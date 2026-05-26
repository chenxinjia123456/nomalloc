/**
 * @file allocator.c
 * @brief Nomalloc核心分配器实现
 * 
 * 本文件实现了nomalloc的核心分配逻辑，包括：
 * - 系统内存分配封装
 * - 分配头管理（header管理）
 * - 初始化和关闭流程
 * - POSIX风格API实现
 * 
 * 关键设计：
 * 
 * 1. 分配头机制（alloc_header）
 * ┌────────────────────────────────────────────┐
 * │ raw_ptr (8B)     │ 原始分配指针            │
 * │ size (8B)        │ 实际分配大小            │
 * │ requested_size   │ 用户请求大小            │
 * │ magic (4B)       │ 验证魔术数 0x4E4F4D41  │
 * │ flags (4B)       │ 对齐值等标志            │
 * ├──────────────────┴─────────────────────────┤
 * │ 用户数据区        │ 返回给用户的指针        │
 * └────────────────────────────────────────────┘
 * 
 * 内存布局：
 * raw_ptr ──► header开始
 * user_ptr ──► header + ALLOC_HEADER_SIZE (返回给用户)
 * 
 * 2. 系统调用封装
 * 使用__libc_malloc/__libc_free/__libc_realloc避免递归调用。
 * nomalloc当前处于原型阶段，直接使用系统分配器。
 * 
 * 3. 初始化保护
 * - initialized标志防止重复初始化
 * - g_allocator_initializing防止初始化期间递归
 * - 初始化期间系统分配器降级
 * 
 * @author chenxinjia123456
 * @date 2026
 */

#define _GNU_SOURCE
#include "allocator.h"
#include "../core/size_class.h"
#include "../memory/arena.h"
#include "../memory/chunk.h"
#include "../cache/tcache.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include "../os/arch/prefetch.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <malloc.h>
#include <sys/mman.h>
#include <unistd.h>

/**
 * @brief 全局分配器实例
 * 
 * 单例模式，所有操作通过此实例。
 */
struct allocator g_allocator;

/**
 * @brief 初始化过程标志
 * 
 * 防止初始化期间的递归调用。
 * 当为true时，malloc/free降级到系统调用。
 */
static bool g_allocator_initializing = false;

/**
 * @defgroup HeaderManagement 分配头管理
 * @brief 分配头用于跟踪内存元数据
 * 
 * 每个分配块前面都有一个alloc_header结构，记录：
 * - 原始分配地址（用于释放）
 * - 分配大小（用于统计）
 * - 用户请求大小（用于realloc/usable_size）
 * - 魔术数（用于验证）
 * - 标志位（如对齐值）
 * @{
 */

/**
 * @def ALLOC_HEADER_SIZE
 * @brief 分配头大小（字节）
 * 
 * sizeof(struct alloc_header) = 32字节（64位系统）
 */
#define ALLOC_HEADER_SIZE (sizeof(struct alloc_header))

/**
 * @def ALLOC_HEADER_ALIGNMENT
 * @brief 分配头对齐要求
 * 
 * 保证用户指针满足16字节对齐，
 * 适合大多数SIMD操作。
 */
#define ALLOC_HEADER_ALIGNMENT 16

/**
 * @struct alloc_header
 * @brief 分配元数据头结构
 * 
 * 存储在每个分配块的用户指针之前：
 * 
 * 内存布局示例：
 * ┌────────────────────────────────────┐
 * │ [raw_ptr] [size] [req_size] [magic]│ ← header (32B)
 * │ [flags] [padding...]               │
 * ├────────────────────────────────────┤
 * │ [用户数据...]                       │ ← user_ptr
 * └────────────────────────────────────┘
 * 
 * 释放流程：
 * 1. user_ptr - ALLOC_HEADER_SIZE = header地址
 * 2. header->raw_ptr = 原始分配地址
 * 3. free(header->raw_ptr)
 */
struct alloc_header {
    /**
     * @brief 原始分配指针
     * 
     * __libc_malloc返回的地址，用于__libc_free。
     * 可能与header地址不同（对齐分配场景）。
     */
    void* raw_ptr;
    
    /**
     * @brief 实际分配大小（含header）
     * 
     * 用于统计和某些特殊情况释放。
     */
    size_t size;
    
    /**
     * @brief 用户请求大小
     * 
     * malloc(size)的原始参数，
     * 用于realloc数据复制和usable_size查询。
     */
    size_t requested_size;
    
    /**
     * @brief 验证魔术数
     * 
     * 固定值ALLOC_MAGIC = 0x4E4F4D41 ("NOMA")
     * 用于识别分配来源和验证header有效性。
     */
    uint32_t magic;
    
    /**
     * @brief 分配标志
     * 
     * 存储对齐值或其他元数据。
     * aligned_alloc时存储alignment参数。
     */
    uint32_t flags;
};

/**
 * @def ALLOC_MAGIC
 * @brief header验证魔术数
 * 
 * ASCII: "NOMA" (nomalloc缩写)
 * 用于快速验证指针是否来自nomalloc。
 */
#define ALLOC_MAGIC 0x4E4F4D41

/** @} */

/**
 * @defgroup SystemAllocators 系统分配器封装
 * @brief 封装libc分配函数，避免递归调用
 * 
 * nomalloc作为内存分配器，内部不能使用malloc/free（会造成递归）。
 * 因此直接调用__libc_malloc/__libc_free等底层函数。
 * 
 * 这些函数仅在以下场景使用：
 * - 分配器初始化期间
 * - 实际内存分配（当前原型阶段）
 * - 内部数据结构分配
 * @{
 */

/**
 * @brief 系统malloc封装
 * 
 * 直接调用__libc_malloc，避免nomalloc递归。
 * 
 * @param size 分配大小
 * @return 分配指针或NULL
 * 
 * @note 初始化期间强制使用此函数
 */
static void* system_malloc(size_t size) {
    extern void* __libc_malloc(size_t);
    return __libc_malloc(size);
}

/**
 * @brief 系统free封装
 * 
 * 直接调用__libc_free，避免nomalloc递归。
 * 
 * @param ptr 要释放的指针
 * 
 * @note 释放raw_ptr时使用此函数
 */
static void system_free(void* ptr) {
    extern void __libc_free(void*);
    __libc_free(ptr);
}

/**
 * @brief 系统realloc封装
 * 
 * 直接调用__libc_realloc。
 * 
 * @param ptr 原指针
 * @param size 新大小
 * @return 新指针或NULL
 * 
 * @note 当前原型未使用（realloc有特殊处理）
 */
static void* system_realloc(void* ptr, size_t size) {
    extern void* __libc_realloc(void*, size_t);
    return __libc_realloc(ptr, size);
}

/**
 * @brief 系统对齐分配（大对齐）
 * 
 * 使用mmap实现大对齐分配（对齐值>=页大小）。
 * 
 * 实现原理：
 * 1. mmap分配total_size = size + alignment
 * 2. 在分配区域内计算对齐地址
 * 3. munmap未使用的头尾部分
 * 
 * 优点：
 * - 支持任意大对齐（>=页大小）
 * - 自然对齐，无需额外结构
 * 
 * 缺点：
 * - 仅适用于大分配
 * - munmap开销较大
 * 
 * @param alignment 对齐要求
 * @param size 分配大小
 * @return 对齐指针或NULL
 */
static void* system_aligned_alloc(size_t alignment, size_t size) {
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_alignment = alignment > page_size ? alignment : page_size;
    size_t total_size = size + aligned_alignment;
    
    void* ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }
    
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned_addr = align_up(addr, alignment);
    
    if (aligned_addr != addr) {
        size_t head_pad = aligned_addr - addr;
        munmap(ptr, head_pad);
    }
    
    size_t tail_pad = total_size - (aligned_addr - addr) - size;
    if (tail_pad > 0) {
        munmap((void*)(aligned_addr + size), tail_pad);
    }
    
    return (void*)aligned_addr;
}

/**
 * @brief 系统对齐释放
 * 
 * munmap释放mmap分配的内存。
 * 
 * @param ptr 对齐指针
 * @param size 分配大小（用于计算）
 * 
 * @note 需要知道原始分配大小
 */
static void system_aligned_free(void* ptr, size_t size) {
    if (!ptr) return;
    munmap(ptr, size);
}

/** @} */

/**
 * @defgroup HeaderOperations 分配头操作函数
 * @brief 管理分配头的创建、查询、提取
 * 
 * 用户指针与raw_ptr的转换：
 * 
 * malloc流程：
 * raw_ptr ──► add_header() ──► user_ptr
 * 
 * free流程：
 * user_ptr ──► get_header() ──► header ──► raw_ptr ──► __libc_free()
 * @{
 */

/**
 * @brief 添加分配头到原始指针
 * 
 * 将__libc_malloc返回的raw_ptr转换为用户指针。
 * 
 * 操作步骤：
 * 1. 在raw_ptr位置写入header数据
 * 2. 计算user_ptr = raw_ptr + ALLOC_HEADER_SIZE
 * 3. 返回user_ptr给用户
 * 
 * 内存布局：
 * ┌────────────────────┐
 * │ raw_ptr ◄── header │ (32字节)
 * │ [magic=ALLOC_MAGIC]│
 * ├────────────────────┤
 * │ user_ptr ◄─────────│ (返回给用户)
 * │ [用户数据...]       │
 * └────────────────────┘
 * 
 * @param raw_ptr 系统malloc返回的原始指针
 * @param size 实际分配大小（含header）
 * @param requested_size 用户请求大小
 * @return 用户指针（raw_ptr + ALLOC_HEADER_SIZE）
 * 
 * @note raw_ptr必须有效
 * @note user_ptr满足16字节对齐
 */
static inline void* add_header(void* raw_ptr, size_t size, size_t requested_size) {
    if (!raw_ptr) return NULL;
    
    struct alloc_header* header = (struct alloc_header*)raw_ptr;
    header->raw_ptr = raw_ptr;
    header->size = size;
    header->requested_size = requested_size;
    header->magic = ALLOC_MAGIC;
    header->flags = 0;
    
    void* user_ptr = (void*)((uintptr_t)raw_ptr + ALLOC_HEADER_SIZE);
    return user_ptr;
}

/**
 * @brief 从用户指针获取分配头
 * 
 * 验证并提取header信息。
 * 
 * 操作步骤：
 * 1. 计算header_addr = user_ptr - ALLOC_HEADER_SIZE
 * 2. 检查地址合法性
 * 3. 验证magic字段
 * 4. 返回header指针
 * 
 * @param user_ptr 用户指针
 * @return header指针，无效返回NULL
 * 
 * @note 用于free、realloc、usable_size
 * @note magic验证确保指针来自nomalloc
 */
static inline struct alloc_header* get_header(void* user_ptr) {
    if (!user_ptr) return NULL;
    
    uintptr_t user_addr = (uintptr_t)user_ptr;
    
    if (user_addr < ALLOC_HEADER_SIZE) {
        return NULL;
    }
    
    uintptr_t header_addr = user_addr - ALLOC_HEADER_SIZE;
    
    struct alloc_header* header = (struct alloc_header*)header_addr;
    
    if (header->magic != ALLOC_MAGIC) {
        return NULL;
    }
    
    return header;
}

/**
 * @brief 从用户指针提取原始指针
 * 
 * 用于需要直接操作raw_ptr的场景。
 * 
 * 流程：
 * user_ptr ──► get_header() ──► header ──► raw_ptr
 * 
 * @param user_ptr 用户指针
 * @return raw_ptr，无效返回user_ptr本身
 * 
 * @note 某些场景指针非nomalloc分配，直接返回
 */
static inline void* remove_header(void* user_ptr) {
    struct alloc_header* header = get_header(user_ptr);
    if (!header) return user_ptr;
    
    uintptr_t header_addr = (uintptr_t)header;
    return (void*)header_addr;
}

/** @} */

/**
 * @defgroup Initialization 初始化和关闭
 * @brief 分配器生命周期管理
 * @{
 */

/**
 * @brief 初始化全局分配器
 * 
 * 执行完整的初始化流程，构建nomalloc运行环境。
 * 
 * 初始化顺序（严格按序执行）：
 * 
 * 1. **状态检查**
 *    - 检查initialized标志（防止重复）
 *    - 检查g_allocator_initializing（防止递归）
 * 
 * 2. **配置初始化**
 *    - region_size: 默认2MB内存区域
 *    - tcache_max_size: 线程缓存容量
 *    - gc_threshold: GC触发阈值
 *    - numa_aware: NUMA优化开关
 *    - log_level: 日志级别
 * 
 * 3. **子系统初始化**
 *    - log系统：输出日志
 *    - arena_manager：管理内存区域
 *    - 默认arena：首个分配区域
 *    - 全局锁：保护关键操作
 *    - 线程key：线程缓存绑定
 *    - 统计原子变量：计数器初始化
 * 
 * 4. **错误处理**
 *    - 任一步骤失败，清理已初始化资源
 *    - 重置initialized标志
 *    - 返回错误码
 * 
 * @return 0 成功
 * @return -1 失败（已初始化/资源不足/子系统失败）
 * 
 * @note 可手动调用或自动触发（首次malloc时）
 * @note 初始化期间malloc降级到__libc_malloc
 * 
 * @see allocator_shutdown()
 */
int allocator_init(void) {
    if (g_allocator.initialized) {
        return 0;
    }
    
    if (g_allocator_initializing) {
        return -1;
    }
    
    g_allocator_initializing = true;
    
    memset(&g_allocator, 0, sizeof(g_allocator));
    
    g_allocator.config.region_size = NOMALLOC_REGION_SIZE_DEFAULT;
    g_allocator.config.tcache_max_size = NOMALLOC_TCACHE_SIZE_DEFAULT;
    g_allocator.config.gc_threshold = NOMALLOC_GC_THRESHOLD_DEFAULT;
    g_allocator.config.gc_watermark_high = NOMALLOC_GC_WATERMARK_HIGH_DEFAULT;
    g_allocator.config.gc_watermark_low = NOMALLOC_GC_WATERMARK_LOW_DEFAULT;
    g_allocator.config.numa_aware = true;
    g_allocator.config.huge_pages = false;
    g_allocator.config.log_level = LOG_LEVEL_INFO;
    g_allocator.config.leak_detection_enabled = false;
    g_allocator.config.stats_enabled = true;
    g_allocator.config.profiler_enabled = false;
    g_allocator.config.profiler_sample_rate = 0.01;
    
    log_init(g_allocator.config.log_level, stderr);
    
    g_allocator.initialized = true;
    
    if (arena_manager_init() != 0) {
        g_allocator_initializing = false;
        g_allocator.initialized = false;
        return -1;
    }
    
    struct arena* arena = arena_create(0);
    if (!arena) {
        arena_manager_shutdown();
        g_allocator_initializing = false;
        g_allocator.initialized = false;
        return -1;
    }
    
    mutex_init(&g_allocator.lock);
    
    if (pthread_key_create(&g_allocator.tcache_key, NULL) != 0) {
        arena_manager_shutdown();
        mutex_destroy(&g_allocator.lock);
        g_allocator_initializing = false;
        g_allocator.initialized = false;
        return -1;
    }
    
    atomic64_init(&g_allocator.total_allocated, 0);
    atomic64_init(&g_allocator.total_freed, 0);
    
    g_allocator_initializing = false;
    
    return 0;
}

/**
 * @brief 关闭全局分配器
 * 
 * 执行完整的清理流程，释放所有资源。
 * 
 * 关闭顺序（与初始化相反）：
 * 
 * 1. **状态保护**
 *    - 设置g_allocator_initializing阻止新分配
 *    - 检查initialized标志
 * 
 * 2. **持锁保护**
 *    - 获取全局锁，防止并发操作
 * 
 * 3. **子系统清理**
 *    - arena_manager_shutdown：释放所有arena
 *    - tcache_destroy：逐个销毁线程缓存
 *    - pthread_key_delete：删除线程key
 * 
 * 4. **统计重置**
 *    - atomic64_store清零计数器
 * 
 * 5. **最终清理**
 *    - 清除initialized标志
 *    - 释放锁并销毁
 *    - memset清零整个结构
 *    - log_shutdown关闭日志
 * 
 * @note 调用后不能再分配（会降级到系统malloc）
 * @note 应在程序退出前调用，避免内存泄漏
 * 
 * @see allocator_init()
 */
void allocator_shutdown(void) {
    g_allocator_initializing = true;
    
    if (!g_allocator.initialized) {
        g_allocator_initializing = false;
        return;
    }
    
    mutex_lock(&g_allocator.lock);
    
    arena_manager_shutdown();
    
    for (size_t i = 0; i < MAX_THREADS; i++) {
        if (g_allocator.tcaches[i]) {
            tcache_destroy(g_allocator.tcaches[i]);
            g_allocator.tcaches[i] = NULL;
        }
    }
    
    pthread_key_delete(g_allocator.tcache_key);
    
    atomic64_store(&g_allocator.total_allocated, 0);
    atomic64_store(&g_allocator.total_freed, 0);
    
    g_allocator.initialized = false;
    
    mutex_unlock(&g_allocator.lock);
    mutex_destroy(&g_allocator.lock);
    
    memset(&g_allocator, 0, sizeof(g_allocator));
    
    log_shutdown();
    
    g_allocator_initializing = false;
}

/** @} */

/**
 * @brief 获取当前线程的线程缓存
 * 
 * 线程缓存（tcache）是nomalloc的核心优化：
 * 
 * 设计原理：
 * - 每线程独立缓存，无锁竞争
 * - 快速路径：直接从缓存分配
 * - 慢速路径：从arena补充缓存
 * 
 * 实现流程：
 * 1. pthread_getspecific查询当前线程tcache
 * 2. 若无缓存，创建新tcache
 * 3. pthread_setspecific绑定到线程
 * 4. 注册到全局tcaches数组（用于shutdown清理）
 * 
 * 性能优势：
 * - 避免全局锁竞争（多线程场景）
 * - 减少arena访问次数
 * - 提高缓存局部性
 * 
 * @return 线程缓存指针，失败返回NULL
 * 
 * @note 首次调用会创建tcache
 * @note 线程退出时tcache保留（需shutdown清理）
 */
struct tcache* allocator_get_tcache(void) {
    struct tcache* tcache = (struct tcache*)pthread_getspecific(g_allocator.tcache_key);
    
    if (!tcache) {
        tcache = tcache_create();
        if (!tcache) {
            log_error("Failed to create tcache for thread");
            return NULL;
        }
        
        pthread_setspecific(g_allocator.tcache_key, tcache);
        
        static atomic32_t tcache_id_counter;
        int id = atomic32_inc_fetch(&tcache_id_counter);
        if (id < MAX_THREADS) {
            g_allocator.tcaches[id] = tcache;
        }
        
        log_debug("Created tcache for thread %lu", pthread_self());
    }
    
    return tcache;
}

/**
 * @defgroup CoreAllocation 核心分配函数
 * @brief 实现POSIX风格的内存分配API
 * 
 * 当前实现状态（原型阶段）：
 * - 直接使用__libc_malloc + header封装
 * - 未来将集成tcache和arena
 * 
 * 设计考虑：
 * - 避免递归（使用__libc_malloc而非malloc）
 * - 数据完整性（header跟踪元数据）
 * - 性能统计（原子计数器）
 * @{
 */

/**
 * @brief 分配内存
 * 
 * 核心分配流程：
 * 
 * 1. **初始化检查**
 *    - 未初始化且非初始化中：触发allocator_init()
 *    - 初始化中：降级到__libc_malloc（避免递归）
 * 
 * 2. **参数处理**
 *    - size=0：调整为1（保证返回有效指针）
 *    - 对齐计算：align_up(size, 16)
 * 
 * 3. **内存分配**
 *    - 计算total_size = aligned_size + header + padding
 *    - 调用__libc_malloc(total_size)
 * 
 * 4. **header添加**
 *    - 在raw_ptr位置写入header
 *    - 计算user_ptr = raw_ptr + ALLOC_HEADER_SIZE
 * 
 * 5. **统计更新**
 *    - atomic64_add_fetch更新total_allocated
 * 
 * 内存布局：
 * ┌─────────────────────────────────────┐
 * │ raw_ptr ◄── [header 32B]            │ __libc_malloc返回
 * │             [magic=ALLOC_MAGIC]      │ 验证标记
 * ├─────────────────────────────────────┤
 * │ user_ptr ◄── [用户数据...]           │ 返回给用户
 * └─────────────────────────────────────┘
 * 
 * @param size 请求大小（字节）
 * @return 用户指针，失败返回NULL
 * 
 * @note user_ptr满足16字节对齐
 * @note 统计只记录requested_size（不含header）
 */
void* allocator_malloc(size_t size) {
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            return system_malloc(size);
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    if (size == 0) {
        size = 1;
    }
    
    size_t aligned_size = align_up(size, ALLOC_HEADER_ALIGNMENT);
    size_t total_size = aligned_size + ALLOC_HEADER_SIZE + ALLOC_HEADER_ALIGNMENT;
    
    extern void* __libc_malloc(size_t);
    void* raw_ptr = __libc_malloc(total_size);
    if (!raw_ptr) {
        return NULL;
    }
    
    void* user_ptr = add_header(raw_ptr, total_size, size);
    
    atomic64_add_fetch(&g_allocator.total_allocated, size);
    
    return user_ptr;
}

/**
 * @brief 释放内存
 * 
 * 释放流程：
 * 
 * 1. **空指针检查**
 *    - ptr=NULL：安全返回（无操作）
 * 
 * 2. **初始化状态**
 *    - 未初始化或初始化中：直接__libc_free
 * 
 * 3. **header提取**
 *    - user_ptr - ALLOC_HEADER_SIZE = header
 *    - 验证magic确保来源
 * 
 * 4. **实际释放**
 *    - free(header->raw_ptr)而非user_ptr
 *    - raw_ptr才是__libc_malloc返回的地址
 * 
 * 5. **统计更新**
 *    - atomic64_add_fetch更新total_freed
 * 
 * 指针来源识别：
 * - magic=ALLOC_MAGIC：nomalloc分配，有header
 * - magic不匹配：可能是系统malloc或其他来源
 * 
 * @param ptr 用户指针（或任意指针）
 * 
 * @note 支持释放非nomalloc指针（兼容性）
 * @note 统计只记录requested_size
 */
void allocator_free(void* ptr) {
    if (!ptr) return;
    
    extern void __libc_free(void*);
    
    if (!g_allocator.initialized || g_allocator_initializing) {
        __libc_free(ptr);
        return;
    }
    
    struct alloc_header* header = get_header(ptr);
    if (header) {
        size_t size = header->requested_size;
        atomic64_add_fetch(&g_allocator.total_freed, size);
        
        __libc_free(header->raw_ptr);
        return;
    }
    
    __libc_free(ptr);
}

/**
 * @brief 分配并清零内存
 * 
 * 实现策略：malloc + memset(0)
 * 
 * 优点：
 * - 复用malloc逻辑
 * - 保证内存清零
 * 
 * 缺点：
 * - 额外memset开销
 * - 未来可优化（tcache可直接返回清零块）
 * 
 * @param nmemb 元素数量
 * @param size 元素大小
 * @return 清零后的指针，失败返回NULL
 * 
 * @note 总大小 = nmemb * size
 * @note 零大小调整为1
 */
void* allocator_calloc(size_t nmemb, size_t size) {
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            extern void* __libc_calloc(size_t, size_t);
            return __libc_calloc(nmemb, size);
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    size_t total_elements = nmemb * size;
    if (total_elements == 0) {
        total_elements = 1;
    }
    
    void* ptr = allocator_malloc(total_elements);
    if (ptr) {
        memset(ptr, 0, total_elements);
    }
    
    return ptr;
}

/**
 * @brief 重新分配内存
 * 
 * realloc是内存分配中最复杂的操作：
 * 
 * 场景处理：
 * 
 * 1. **ptr=NULL**
 *    等同于malloc(size)
 * 
 * 2. **size=0**
 *    等同于free(ptr)，返回NULL
 * 
 * 3. **缩小（size < old_size）**
 *    直接修改header->requested_size
 *    不释放多余内存（优化：避免频繁分配）
 * 
 * 4. **扩大（size > old_size）**
 *    - 分配新内存
 *    - 复制旧数据（old_requested_size字节）
 *    - 释放旧内存
 *    - 返回新指针
 * 
 * 数据完整性保证：
 * - 扩大时完整复制旧数据
 * - 缩小时保留原有数据
 * - 失败时旧指针保持有效
 * 
 * @param ptr 原指针
 * @param size 新大小
 * @return 新指针（可能与ptr相同），失败返回NULL
 * 
 * @note 失败时旧内存不被释放
 * @note 可能返回不同地址（扩大场景）
 */
void* allocator_realloc(void* ptr, size_t size) {
    extern void* __libc_realloc(void*, size_t);
    extern void* __libc_malloc(size_t);
    extern void __libc_free(void*);
    
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            return __libc_realloc(ptr, size);
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    if (!ptr) {
        return allocator_malloc(size);
    }
    
    if (size == 0) {
        allocator_free(ptr);
        return NULL;
    }
    
    struct alloc_header* header = get_header(ptr);
    if (!header) {
        return __libc_realloc(ptr, size);
    }
    
    size_t old_requested_size = header->requested_size;
    
    if (size <= old_requested_size) {
        header->requested_size = size;
        return ptr;
    }
    
    size_t aligned_size = align_up(size, ALLOC_HEADER_ALIGNMENT);
    size_t new_total_size = aligned_size + ALLOC_HEADER_SIZE + ALLOC_HEADER_ALIGNMENT;
    
    void* new_raw_ptr = __libc_malloc(new_total_size);
    if (!new_raw_ptr) {
        return NULL;
    }
    
    memcpy((void*)((uintptr_t)new_raw_ptr + ALLOC_HEADER_SIZE), ptr, old_requested_size);
    
    struct alloc_header* new_header = (struct alloc_header*)new_raw_ptr;
    new_header->raw_ptr = new_raw_ptr;
    new_header->size = new_total_size;
    new_header->requested_size = size;
    new_header->magic = ALLOC_MAGIC;
    new_header->flags = 0;
    
    void* user_ptr = (void*)((uintptr_t)new_raw_ptr + ALLOC_HEADER_SIZE);
    
    atomic64_add_fetch(&g_allocator.total_freed, old_requested_size);
    atomic64_add_fetch(&g_allocator.total_allocated, size);
    
    __libc_free(header->raw_ptr);
    
    return user_ptr;
}

/**
 * @brief 分配对齐内存
 * 
 * 对齐分配的特殊挑战：
 * - 需要返回严格对齐的user_ptr
 * - header位置需要调整（可能不在raw_ptr）
 * - 需要记录raw_ptr用于释放
 * 
 * 实现策略：
 * 
 * 1. **分配预留空间**
 *    total_size = size + alignment + header
 *    确保有足够空间调整对齐
 * 
 * 2. **计算对齐地址**
 *    user_addr = align_up(raw_addr + header, alignment)
 * 
 * 3. **header位置调整**
 *    header_addr = user_addr - ALLOC_HEADER_SIZE
 *    header可能在raw_ptr之后的某个位置
 * 
 * 4. **记录信息**
 *    header->raw_ptr = 原始分配地址
 *    header->flags = alignment（记录对齐值）
 * 
 * 内存布局示例（alignment=64）：
 * ┌─────────────────────────────────────┐
 * │ raw_ptr ◄── [未使用区域]            │ __libc_malloc返回
 * │             [padding...]            │
 * ├─────────────────────────────────────┤
 * │ header ◄─── [header 32B]            │ 可能偏移
 * │             [magic] [flags=64]       │ 记录对齐值
 * ├─────────────────────────────────────┤
 * │ user_ptr ◄── [用户数据...]           │ 严格64字节对齐
 * └─────────────────────────────────────┘
 * 
 * @param alignment 对齐要求（必须是2的幂）
 * @param size 请求大小
 * @return 对齐指针，失败返回NULL
 * 
 * @note 验证alignment是2的幂
 * @note flags字段记录对齐值
 */
void* allocator_aligned_alloc(size_t alignment, size_t size) {
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            extern void* __libc_malloc(size_t);
            
            size_t alloc_size = size + alignment + ALLOC_HEADER_SIZE;
            void* raw_ptr = __libc_malloc(alloc_size);
            if (!raw_ptr) return NULL;
            
            uintptr_t raw_addr = (uintptr_t)raw_ptr;
            uintptr_t user_addr_target = align_up(raw_addr + ALLOC_HEADER_SIZE, alignment);
            uintptr_t header_addr = user_addr_target - ALLOC_HEADER_SIZE;
            
            struct alloc_header* header = (struct alloc_header*)header_addr;
            header->raw_ptr = raw_ptr;
            header->size = alloc_size;
            header->requested_size = size;
            header->magic = ALLOC_MAGIC;
            header->flags = alignment;
            
            return (void*)user_addr_target;
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    if (!is_power_of_two(alignment) || alignment == 0) {
        return NULL;
    }
    
    if (size == 0) {
        size = 1;
    }
    
    extern void* __libc_malloc(size_t);
    
    size_t alloc_size = size + alignment + ALLOC_HEADER_SIZE;
    void* raw_ptr = __libc_malloc(alloc_size);
    if (!raw_ptr) return NULL;
    
    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t user_addr_target = align_up(raw_addr + ALLOC_HEADER_SIZE, alignment);
    uintptr_t header_addr = user_addr_target - ALLOC_HEADER_SIZE;
    
    struct alloc_header* header = (struct alloc_header*)header_addr;
    header->raw_ptr = raw_ptr;
    header->size = alloc_size;
    header->requested_size = size;
    header->magic = ALLOC_MAGIC;
    header->flags = alignment;
    
    return (void*)user_addr_target;
}

/**
 * @brief 获取分配的实际可用大小
 * 
 * 从header提取requested_size。
 * 实际可用大小可能大于请求大小（因对齐）。
 * 
 * @param ptr 用户指针
 * @return 可用大小，无效指针返回0
 * 
 * @note 对应glibc的malloc_usable_size()
 * @note 不含header开销
 */
size_t allocator_malloc_usable_size(void* ptr) {
    if (!ptr) return 0;
    
    struct alloc_header* header = get_header(ptr);
    if (!header) {
        return 0;
    }
    
    return header->requested_size;
}

/** @} */

/**
 * @defgroup Configuration 配置和统计
 * @brief 分配器配置管理与统计输出
 * @{
 */

/**
 * @brief 配置分配器参数
 * 
 * 动态调整分配器行为，支持运行时优化。
 * 
 * 可配置参数：
 * 
 * 1. **内存管理参数**
 *    - region_size: Arena内存区域大小（影响大分配）
 *    - tcache_max_size: 线程缓存容量（影响小分配性能）
 *    - gc_threshold: GC触发阈值（影响内存回收频率）
 *    - gc_watermark_high/low: GC水位线（控制GC激进程度）
 * 
 * 2. **系统优化参数**
 *    - numa_aware: NUMA优化开关（多CPU系统性能）
 *    - huge_pages: 大页支持（减少TLB miss）
 * 
 * 3. **调试参数**
 *    - log_level: 日志输出级别（NONE~TRACE）
 *    - leak_detection_enabled: 泄漏检测开关
 *    - stats_enabled: 统计收集开关
 * 
 * 4. **分析参数**
 *    - profiler_enabled: 性能分析开关
 *    - profiler_sample_rate: 采样率（0.0~1.0）
 * 
 * 参数验证：
 * - 范围检查（如region_size必须在MIN~MAX之间）
 * - 逻辑检查（如gc_watermark_high必须>low）
 * - 无效参数保持原值不变
 * 
 * @param config 配置结构体指针
 * @return 0 成功，-1 失败（config为NULL）
 * 
 * @note 需持锁执行，保证线程安全
 * @note 部分参数立即生效，部分需重启
 */
int allocator_configure(const struct nomalloc_allocator_config* config) {
    if (!config) {
        return -1;
    }
    
    mutex_lock(&g_allocator.lock);
    
    if (config->region_size >= NOMALLOC_REGION_SIZE_MIN && 
        config->region_size <= NOMALLOC_REGION_SIZE_MAX) {
        g_allocator.config.region_size = config->region_size;
    }
    
    if (config->tcache_max_size >= NOMALLOC_TCACHE_SIZE_MIN) {
        g_allocator.config.tcache_max_size = config->tcache_max_size;
    }
    
    if (config->gc_threshold > 0) {
        g_allocator.config.gc_threshold = config->gc_threshold;
    }
    
    if (config->gc_watermark_high > config->gc_watermark_low) {
        g_allocator.config.gc_watermark_high = config->gc_watermark_high;
        g_allocator.config.gc_watermark_low = config->gc_watermark_low;
    }
    
    g_allocator.config.numa_aware = config->numa_aware;
    g_allocator.config.huge_pages = config->huge_pages;
    
    if (config->log_level >= LOG_LEVEL_NONE && config->log_level <= LOG_LEVEL_TRACE) {
        g_allocator.config.log_level = config->log_level;
        log_set_level(config->log_level);
    }
    
    g_allocator.config.leak_detection_enabled = config->leak_detection_enabled;
    g_allocator.config.stats_enabled = config->stats_enabled;
    g_allocator.config.profiler_enabled = config->profiler_enabled;
    
    if (config->profiler_sample_rate >= 0.0 && config->profiler_sample_rate <= 1.0) {
        g_allocator.config.profiler_sample_rate = config->profiler_sample_rate;
    }
    
    mutex_unlock(&g_allocator.lock);
    
    log_info("Allocator configuration updated");
    return 0;
}

/**
 * @brief 打印分配器统计信息
 * 
 * 输出详细的运行统计，用于：
 * - 性能分析
 * - 内存监控
 * - 调试诊断
 * 
 * 输出内容：
 * 
 * 1. **全局状态**
 *    - initialized: 初始化状态
 *    - total_allocated: 累计分配字节数
 *    - total_freed: 累计释放字节数
 *    - active_allocations: 当前活跃内存量
 * 
 * 2. **配置参数**
 *    - region_size: 内存区域大小
 *    - tcache_max_size: 线程缓存容量
 *    - gc_threshold: GC阈值
 *    - numa_aware: NUMA状态
 *    - huge_pages: 大页状态
 *    - log_level: 日志级别
 * 
 * 3. **子系统统计**
 *    - arena统计（调用arena_manager_print_stats）
 *    - 未来将添加tcache、GC统计
 * 
 * 输出格式：人类可读文本，适合调试输出。
 * 
 * @note 输出到stdout
 * @note 不持锁，可能读到不一致数据（适合调试而非生产）
 */
void allocator_print_stats(void) {
    printf("\nAllocator Statistics:\n");
    printf("  Initialized: %s\n", g_allocator.initialized ? "yes" : "no");
    printf("  Total allocated: %llu bytes\n", allocator_get_total_allocated());
    printf("  Total freed: %llu bytes\n", allocator_get_total_freed());
    printf("  Active allocations: %llu bytes\n", allocator_get_active_allocations());
    printf("  Region size: %zu bytes\n", g_allocator.config.region_size);
    printf("  Tcache max size: %zu bytes\n", g_allocator.config.tcache_max_size);
    printf("  GC threshold: %zu bytes\n", g_allocator.config.gc_threshold);
    printf("  NUMA aware: %s\n", g_allocator.config.numa_aware ? "yes" : "no");
    printf("  Huge pages: %s\n", g_allocator.config.huge_pages ? "yes" : "no");
    printf("  Log level: %d\n", g_allocator.config.log_level);
    printf("\n");
    
    arena_manager_print_stats();
}

/** @} */