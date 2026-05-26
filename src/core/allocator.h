/**
 * @file allocator.h
 * @brief Nomalloc核心分配器头文件
 * 
 * 本文件定义了nomalloc的核心分配器结构和接口。allocator是nomalloc的核心组件，
 * 负责协调内存分配的各个子系统：
 * 
 * 架构层次（自上而下）：
 * ┌─────────────────────────────────────────────┐
 * │           POSIX API (posix_api.c)           │  用户接口层
 * ├─────────────────────────────────────────────┤
 * │        Allocator (allocator.c)              │  核心协调层
 * ├──────────┬──────────┬──────────┬───────────┤
 * │  Tcache  │  Arena   │   GC     │   Chunk   │  功能子系统层
 * ├──────────┴──────────┴──────────┴───────────┤
 * │         OS Layer (numa/hugepages)          │  操作系统层
 * └─────────────────────────────────────────────┘
 * 
 * 设计理念：
 * 1. 分层架构 - 每层专注单一职责，便于测试和维护
 * 2. 线程缓存 - 每线程独立缓存，减少锁竞争
 * 3. Arena管理 - 内存区域化管理，支持NUMA
 * 4. 延迟回收 - GC机制避免频繁释放
 * 
 * @author chenxinjia123456
 * @date 2026
 */

#ifndef NOMALLOC_CORE_ALLOCATOR_H
#define NOMALLOC_CORE_ALLOCATOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <nomalloc/types.h>
#include "../utils/atomic.h"
#include "../utils/mutex.h"
#include "../utils/log.h"
#include "../utils/assert.h"
#include "../memory/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def MAX_THREADS
 * @brief 支持的最大线程数
 * 
 * 决定了线程缓存数组的最大容量，也影响全局统计的线程维度。
 * 默认值来自nomalloc配置，通常为128。
 */
#ifndef MAX_THREADS
#define MAX_THREADS NOMALLOC_MAX_THREADS
#endif

/**
 * @struct allocator
 * @brief 全局分配器结构体
 * 
 * 这是nomalloc的核心控制结构，管理整个内存分配器的状态和资源。
 * 采用单例模式（全局唯一实例g_allocator），提供全局协调功能。
 * 
 * 内存布局示意：
 * ┌─────────────────────────────────────┐
 * │ initialized: 初始化标志              │
 * │ config: 配置参数                     │
 * │ tcaches[]: 线程缓存数组              │
 * │ tcache_key: 线程局部存储key          │
 * │ gc: 垃圾回收器                       │
 * │ stats: 统计收集器                    │
 * │ leak_detector: 泄漏检测器            │
 * │ profiler: 性能分析器                 │
 * │ total_allocated: 累计分配字节数      │
 * │ total_freed: 累计释放字节数          │
 * │ lock: 全局互斥锁                     │
 * └─────────────────────────────────────┘
 */
struct allocator {
    /**
     * @brief 分配器是否已初始化
     * 
     * 用于防止重复初始化和检测使用状态。
     * 初始化流程会设置此标志，shutdown时清除。
     */
    bool initialized;
    
    /**
     * @brief 分配器配置参数
     * 
     * 包含region大小、tcache大小、GC阈值、NUMA策略等。
     * 可通过allocator_configure()动态调整。
     */
    struct nomalloc_allocator_config config;
    
    /**
     * @brief 线程缓存数组
     * 
     * 每个线程拥有独立的tcache，减少锁竞争。
     * 索引0~MAX_THREADS-1对应线程ID。
     */
    struct tcache* tcaches[MAX_THREADS];
    
    /**
     * @brief 线程局部存储key
     * 
     * 用于pthread_getspecific/pthread_setspecific，
     * 实现线程缓存自动绑定。
     */
    pthread_key_t tcache_key;
    
    /**
     * @brief 垃圾回收器指针
     * 
     * 实现延迟释放机制，避免频繁系统调用。
     * 具体实现见gc.c。
     */
    void* gc;
    
    /**
     * @brief 统计收集器指针
     * 
     * 收集分配/释放次数、吞吐量、延迟等指标。
     * 具体实现见stats_collector.c。
     */
    void* stats;
    
    /**
     * @brief 内存泄漏检测器指针
     * 
     * 记录分配点，检测未释放内存。
     * 具体实现见leak_detector.c。
     */
    void* leak_detector;
    
    /**
     * @brief 性能分析器指针
     * 
     * 采样分配行为，生成性能报告。
     * 支持可配置采样率。
     */
    void* profiler;
    
    /**
     * @brief 累计分配字节数（原子变量）
     * 
     * 所有malloc/calloc/realloc的总分配量。
     * 用于统计和监控。
     */
    atomic64_t total_allocated;
    
    /**
     * @brief 累计释放字节数（原子变量）
     * 
     * 所有free的总释放量。
     * 与total_allocated配合计算活跃内存。
     */
    atomic64_t total_freed;
    
    /**
     * @brief 全局互斥锁
     * 
     * 保护初始化、配置修改等全局操作。
     * 分配/释放路径尽量不使用此锁。
     */
    mutex_t lock;
};

/**
 * @brief 全局分配器实例（单例）
 * 
 * 所有分配操作通过此实例协调。
 * 在allocator_init()中初始化。
 */
extern struct allocator g_allocator;

/**
 * @brief 初始化全局分配器
 * 
 * 执行以下初始化步骤：
 * 1. 配置默认参数
 * 2. 初始化arena管理器
 * 3. 创建默认arena
 * 4. 初始化全局锁
 * 5. 创建线程局部存储key
 * 6. 初始化原子统计变量
 * 
 * @return 0 成功
 * @return -1 失败（如重复初始化、资源不足）
 * 
 * @note 可自动调用，首次malloc时会自动初始化
 * @note 初始化期间使用系统malloc避免递归
 */
int allocator_init(void);

/**
 * @brief 关闭全局分配器
 * 
 * 执行以下清理步骤：
 * 1. 销毁所有线程缓存
 * 2. 关闭arena管理器
 * 3. 删除线程局部存储key
 * 4. 重置统计变量
 * 5. 清除初始化标志
 * 
 * @note 调用后不能再进行分配操作
 * @note 应在程序退出前调用
 */
void allocator_shutdown(void);

/**
 * @brief 分配内存
 * 
 * 核心分配流程：
 * 1. 检查初始化状态
 * 2. 处理零大小请求
 * 3. 计算对齐大小
 * 4. 调用系统malloc
 * 5. 添加分配头信息
 * 6. 更新统计计数
 * 
 * @param size 请求分配的字节数
 * @return 成功返回用户指针，失败返回NULL
 * 
 * @note 当前实现使用系统malloc + header封装
 * @note 未来将集成tcache和arena
 */
void* allocator_malloc(size_t size);

/**
 * @brief 释放内存
 * 
 * 释放流程：
 * 1. 检查空指针
 * 2. 检查初始化状态
 * 3. 从用户指针提取header
 * 4. 调用系统free释放原始指针
 * 5. 更新统计计数
 * 
 * @param ptr 要释放的指针（用户指针）
 * 
 * @note 支持NULL指针（安全无操作）
 * @note 自动识别分配来源（header magic验证）
 */
void allocator_free(void* ptr);

/**
 * @brief 分配并清零内存
 * 
 * 实现为malloc + memset(0)
 * 
 * @param nmemb 元素数量
 * @param size 每个元素大小
 * @return 成功返回清零后的指针，失败返回NULL
 * 
 * @note 保证返回的内存全部为零
 */
void* allocator_calloc(size_t nmemb, size_t size);

/**
 * @brief 重新分配内存
 * 
 * 重分配流程：
 * 1. 处理NULL指针（等同于malloc）
 * 2. 处理零大小（等同于free）
 * 3. 检查缩小请求（直接修改header）
 * 4. 执行扩大请求：
 *    - 分配新内存
 *    - 复制旧数据
 *    - 释放旧内存
 *    - 添加新header
 * 
 * @param ptr 原指针
 * @param size 新大小
 * @return 成功返回新指针，失败返回NULL
 * 
 * @note 保证数据完整性（缩小保留数据，扩大复制数据）
 * @note 可能返回不同地址
 */
void* allocator_realloc(void* ptr, size_t size);

/**
 * @brief 分配对齐内存
 * 
 * 对齐分配流程：
 * 1. 验证对齐参数（必须是2的幂）
 * 2. 计算总分配大小（包含对齐预留）
 * 3. 系统malloc原始内存
 * 4. 计算对齐后的用户地址
 * 5. 在用户地址前放置header
 * 6. 记录对齐值到flags
 * 
 * @param alignment 对齐要求（必须是2的幂）
 * @param size 请求大小
 * @return 成功返回对齐指针，失败返回NULL
 * 
 * @note 用户指针严格满足对齐要求
 * @note header可能不在原始分配起始位置
 */
void* allocator_aligned_alloc(size_t alignment, size_t size);

/**
 * @brief 获取分配的实际可用大小
 * 
 * 从header提取requested_size字段，
 * 返回用户实际可用的大小（可能大于请求大小）。
 * 
 * @param ptr 用户指针
 * @return 可用大小，无效指针返回0
 * 
 * @note 对应glibc的malloc_usable_size()
 */
size_t allocator_malloc_usable_size(void* ptr);

/**
 * @brief 配置分配器参数
 * 
 * 动态调整分配器行为，包括：
 * - region大小
 * - tcache大小
 * - GC阈值
 * - NUMA策略
 * - 日志级别
 * - 分析器配置
 * 
 * @param config 配置结构体指针
 * @return 0 成功，-1 失败
 * 
 * @note 需持锁执行
 * @note 参数有范围验证
 */
int allocator_configure(const struct nomalloc_allocator_config* config);

/**
 * @brief 打印分配器统计信息
 * 
 * 输出到stdout，包括：
 * - 初始化状态
 * - 总分配/释放字节数
 * - 活跃内存量
 * - 配置参数
 * - arena统计
 */
void allocator_print_stats(void);

/**
 * @brief 检查分配器是否已初始化
 * 
 * 快速检查initialized标志，用于：
 * - API入口验证
 * - 条件初始化判断
 * 
 * @return true 已初始化
 * @return false 未初始化
 */
static inline bool allocator_is_initialized(void) {
    return g_allocator.initialized;
}

/**
 * @brief 获取累计分配字节数
 * 
 * 原子读取total_allocated，适合：
 * - 统计报告
 * - 内存监控
 * - 性能分析
 * 
 * @return 累计分配字节数
 */
static inline size_t allocator_get_total_allocated(void) {
    return atomic64_load(&g_allocator.total_allocated);
}

/**
 * @brief 获取累计释放字节数
 * 
 * 原子读取total_freed，适合：
 * - 统计报告
 * - 内存监控
 * - 泄漏检测
 * 
 * @return 累计释放字节数
 */
static inline size_t allocator_get_total_freed(void) {
    return atomic64_load(&g_allocator.total_freed);
}

/**
 * @brief 获取当前活跃内存量
 * 
 * 计算公式：total_allocated - total_freed
 * 
 * 表示当前程序持有的内存量，适合：
 * - 内存使用监控
 * - 泄漏检测
 * - 负载分析
 * 
 * @return 活跃内存字节数
 */
static inline size_t allocator_get_active_allocations(void) {
    return allocator_get_total_allocated() - allocator_get_total_freed();
}

#ifdef __cplusplus
}
#endif

#endif