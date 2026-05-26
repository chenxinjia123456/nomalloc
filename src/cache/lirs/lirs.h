/**
 * @file lirs.h
 * @brief LIRS (Low Inter-reference Recency Set) 缓存替换算法
 * 
 * LIRS是一种高级缓存替换算法，比传统LRU算法更能有效处理各种访问模式，
 * 特别是循环扫描和扫描密集型工作负载。
 * 
 * ## 算法原理
 * 
 * LIRS的核心思想是基于**重用距离**(Reuse Distance)进行分类：
 * 
 * - **重用距离**：两次连续访问同一数据之间访问的其他数据量
 * - **低重用距离(LIR)**：近期被频繁访问，重用距离小 → 保留在缓存
 * - **高重用距离(HIR)**：不频繁访问或首次访问，重用距离大 → 可能淘汰
 * 
 * ## 核心组件
 * 
 * LIRS维护两个主要数据结构：
 * 
 * ```
 * ┌────────────────────────────────────────────────────────────┐
 * │                    S Stack (LIR + HIR历史)                  │
 * │  [新访问] ────────────────────────────────────► [老访问]     │
 * │   LIR块↑   HIR块↑   HIR块↑   HIR块↑   LIR块↑                 │
 * │   ↓命中    ↑晋升    ↑记录    ↑记录    ↓降级                 │
 * ├────────────────────────────────────────────────────────────┤
 * │                    Q Queue (仅HIR驻留块)                    │
 * │  [老HIR] ────────────────────────────────────► [新HIR]      │
 * │   ↓淘汰   ↓淘汰   ↓淘汰                                    │
 * └────────────────────────────────────────────────────────────┘
 * ```
 * 
 * - **S Stack**：记录所有块的访问历史（LIR+HIR），栈顶是最新访问
 * - **Q Queue**：FIFO队列，仅存储驻留的HIR块，队列头部是最老HIR
 * 
 * ## 块类型分类
 * 
 * ```
 * ┌────────────┬─────────────────┬───────────────────────┐
 * │ 块类型     │ 位置            │ 特性                   │
 * ├────────────┼─────────────────┼───────────────────────┤
 * │ LIR        │ S栈+缓存驻留    │ 永不淘汰，直到降级     │
 * │ HIR驻留    │ Q队列+缓存驻留  │ 可被淘汰               │
 * │ HIR非驻留  │ 仅S栈           │ 已淘汰，仅保留历史     │
 * └────────────┴─────────────────┴───────────────────────┘
 * ```
 * 
 * ## 关键操作
 * 
 * 1. **访问LIR块**：
 *    - 移动到S栈顶
 *    - 清理S栈底（移除HIR非驻留块）
 * 
 * 2. **访问HIR驻留块**：
 *    - 若在S栈中 → 晋升为LIR
 *    - S栈底的LIR降级为HIR
 *    - 移动到S栈顶和Q队列尾
 * 
 * 3. **访问HIR非驻留块**：
 *    - 缓存未命中
 *    - 若在S栈中 → 标记为需要晋升
 *    - 从S栈移除并销毁
 * 
 * 4. **插入新块**：
 *    - 初始为HIR驻留
 *    - 若S栈空间充足 → 直接设为LIR
 *    - 放入S栈顶和Q队列尾
 * 
 * ## 性能特点
 * 
 * - **循环扫描抗性**：扫描操作不会冲掉热点数据
 * - **高命中率**：相比LRU提升10-20%（某些场景）
 * - **低碎片**：基于大小而非数量限制
 * - **自适应**：自动调整LIR/HIR比例
 * 
 * ## 应用场景
 * 
 * nomalloc中使用LIRS管理线程缓存(tcache)：
 * - 缓存常用大小的内存块
 * - 避免扫描型分配冲掉热点块
 * - 提高内存分配命中率
 * 
 * 参考文献：
 * - "LIRS: An Efficient Low Inter-reference Recency Set Replacement Policy"
 *   Song Jiang and Xiaodong Zhang, ISCA 2002
 * 
 * @author chenxinjia123456
 * @date 2026
 */

#ifndef NOMALLOC_CACHE_LIRS_LIRS_H
#define NOMALLOC_CACHE_LIRS_LIRS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../../utils/atomic.h"
#include "../../utils/spinlock.h"
#include "../../utils/list.h"
#include "../../utils/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup LIRSConstants LIRS常量配置
 * @{
 */

/**
 * @def LIRS_DEFAULT_CAPACITY
 * @brief 默认缓存容量（块数量）
 * 
 * 影响S栈最大大小和LIR块数量上限。
 */
#define LIRS_DEFAULT_CAPACITY 1024

/**
 * @def LIRS_HIR_RATIO
 * @brief HIR块比例（占总容量的比例）
 * 
 * 默认1%，意味着99%的空间用于LIR块。
 * 较小的HIR比例提高命中率，但降低扫描抗性。
 * 
 * 调优建议：
 * - 扫描密集型场景：增大到2-5%
 * - 热点集中场景：减小到0.5-1%
 */
#define LIRS_HIR_RATIO 0.01

/**
 * @def LIRS_MIN_HIR_BLOCKS
 * @brief 最小HIR块数量
 * 
 * 保证至少有2个HIR驻留块，避免Q队列空导致无法淘汰。
 */
#define LIRS_MIN_HIR_BLOCKS 2

/** @} */

/**
 * @defgroup LIRSBlockTypes LIRS块类型定义
 * @{
 */

/**
 * @enum lirs_block_type_t
 * @brief LIRS块的类型分类
 * 
 * 三种类型决定块的驻留状态和淘汰行为：
 */
typedef enum {
    /**
     * @brief LIR块（低重用距离）
     * 
     * 特性：
     * - 永久驻留缓存（直到降级）
     * - 位于S栈（可能有多个位置）
     * - 不进入Q队列
     * - 访问时移动到S栈顶
     * 
     * 晋升来源：
     * - HIR驻留块在S栈中被访问
     */
    LIRS_BLOCK_LIR,
    
    /**
     * @brief HIR驻留块（高重用距离，仍在缓存）
     * 
     * 特性：
     * - 可被淘汰（从Q队列头部）
     * - 位于Q队列尾部（最新HIR）
     * - 可能位于S栈（记录历史）
     * - 访问时可能晋升为LIR
     * 
     * 来源：
     * - 新插入的块
     * - LIR降级
     */
    LIRS_BLOCK_HIR_RESIDENT,
    
    /**
     * @brief HIR非驻留块（已从缓存淘汰）
     * 
     * 特性：
     * - 仅存在于S栈（记录历史）
     * - 不占用缓存空间
     * - 访问时缓存未命中
     * - 若在S栈被访问，下次插入会晋升
     * 
     * 淘汰来源：
     * - HIR驻留块从Q队列淘汰
     * - 从S栈底部清理移除
     */
    LIRS_BLOCK_HIR_NON_RESIDENT
} lirs_block_type_t;

/** @} */

/**
 * @struct lirs_block
 * @brief LIRS缓存块结构体
 * 
 * 代表缓存中的单个块，存储键值对和元数据。
 * 
 * 内存布局：
 * ┌───────────────────────────────────────┐
 * │ [key] [value] [size]                  │ 用户数据
 * │ [type] [access_time] [reuse_distance] │ LIRS元数据
 * │ [in_s_stack] [in_q_queue]             │ 位置标志
 * │ [s_list] [q_list] [hash_node]         │ 链表节点
 * └───────────────────────────────────────┘
 */
struct lirs_block {
    /**
     * @brief 缓存键（通常是内存块指针或大小类别）
     */
    void* key;
    
    /**
     * @brief 缓存值（实际内存块指针）
     */
    void* value;
    
    /**
     * @brief 块大小（字节）
     * 
     * 用于基于大小的缓存容量控制。
     */
    size_t size;
    
    /**
     * @brief 块类型（LIR/HIR驻留/HIR非驻留）
     */
    lirs_block_type_t type;
    
    /**
     * @brief 最后访问时间（单调递增计数器）
     * 
     * 用于计算重用距离。
     */
    uint64_t access_time;
    
    /**
     * @brief 重用距离（两次访问间隔）
     * 
     * 决定块的类型和淘汰优先级。
     */
    uint64_t reuse_distance;
    
    /**
     * @brief 是否在S栈中
     * 
     * HIR块可能不在S栈（首次访问未记录）。
     */
    bool in_s_stack;
    
    /**
     * @brief 是否在Q队列中
     * 
     * 仅HIR驻留块在Q队列。
     * LIR块和HIR非驻留块不在Q队列。
     */
    bool in_q_queue;
    
    /**
     * @brief S栈链表节点
     */
    struct list_head s_list;
    
    /**
     * @brief Q队列链表节点
     */
    struct list_head q_list;
    
    /**
     * @brief 哈希表节点（用于快速查找）
     */
    struct hlist_node hash_node;
};

/**
 * @struct lirs_stats
 * @brief LIRS缓存统计信息
 * 
 * 用于性能监控和调试分析。
 */
struct lirs_stats {
    /**
     * @brief 缓存命中次数
     */
    atomic64_t hits;
    
    /**
     * @brief 缓存未命中次数
     */
    atomic64_t misses;
    
    /**
     * @brief 淘汰次数（HIR块从缓存移除）
     */
    atomic64_t evictions;
    
    /**
     * @brief 晋升次数（HIR→LIR）
     */
    atomic64_t promotions;
    
    /**
     * @brief 降级次数（LIR→HIR）
     */
    atomic64_t demotions;
    
    /**
     * @brief 当前LIR块数量
     */
    atomic64_t lir_count;
    
    /**
     * @brief 当前HIR驻留块数量
     */
    atomic64_t hir_resident_count;
    
    /**
     * @brief 当前HIR非驻留块数量（仅S栈中）
     */
    atomic64_t hir_non_resident_count;
    
    /**
     * @brief 当前缓存总大小（字节）
     */
    atomic64_t total_size;
    
    /**
     * @brief 缓存最大大小限制（字节）
     */
    atomic64_t max_size;
};

/**
 * @struct lirs_cache
 * @brief LIRS缓存主结构
 * 
 * 管理整个LIRS缓存的状态和数据结构。
 */
struct lirs_cache {
    /**
     * @brief 缓存容量（块数量）
     */
    size_t capacity;
    
    /**
     * @brief HIR容量（Q队列最大大小）
     * 
     * = capacity * LIRS_HIR_RATIO
     */
    size_t hir_capacity;
    
    /**
     * @brief 最大缓存大小（字节）
     */
    size_t max_size;
    
    /**
     * @brief S栈（访问历史栈）
     * 
     * 栈顶：最新访问
     * 栈底：最老访问（可能被清理）
     */
    struct list_head s_stack;
    
    /**
     * @brief Q队列（HIR驻留块FIFO队列）
     * 
     * 队头：最老HIR（淘汰候选）
     * 队尾：最新HIR
     */
    struct list_head q_queue;
    
    /**
     * @brief S栈当前大小
     */
    size_t s_stack_size;
    
    /**
     * @brief Q队列当前大小
     */
    size_t q_queue_size;
    
    /**
     * @brief 哈希表（快速查找块）
     */
    struct hlist_head* hash_table;
    
    /**
     * @brief 哈希表大小
     */
    size_t hash_table_size;
    
    /**
     * @brief 哈希表掩码（用于快速取模）
     */
    size_t hash_table_mask;
    
    /**
     * @brief 自旋锁（保护并发访问）
     */
    spinlock_t lock;
    
    /**
     * @brief 统计信息
     */
    struct lirs_stats stats;
    
    /**
     * @brief 当前时间计数器（单调递增）
     */
    uint64_t current_time;
    
    /**
     * @brief 淘汰回调函数
     * 
     * 当块被淘汰时调用，通知用户释放资源。
     */
    void (*evict_callback)(void* key, void* value, size_t size);
    
    /**
     * @brief 用户自定义数据
     */
    void* user_data;
};

/**
 * @defgroup LIRSAPI LIRS缓存API
 * @brief 缓存创建、查询、插入、删除操作
 * @{
 */

/**
 * @brief 创建LIRS缓存
 * 
 * 初始化缓存结构，分配哈希表，设置容量限制。
 * 
 * @param capacity 块数量容量（影响S栈和Q队列大小）
 * @param max_size 最大缓存大小（字节，基于大小淘汰）
 * @return 缓存指针，失败返回NULL
 * 
 * @note hir_capacity自动计算为capacity * LIRS_HIR_RATIO
 * @note 哈希表大小为capacity的2倍（减少冲突）
 */
struct lirs_cache* lirs_create(size_t capacity, size_t max_size);

/**
 * @brief销毁LIRS缓存
 * 
 * 释放所有块和资源，调用淘汰回调。
 * 
 * @param cache 缓存指针
 * 
 * @note 会触发所有块的evict_callback
 */
void lirs_destroy(struct lirs_cache* cache);

/**
 * @brief 从缓存获取值
 * 
 * 核心查询操作，根据块类型执行不同处理：
 * 
 * - **LIR块命中**：移动到S栈顶，清理栈底
 * - **HIR驻留命中**：若在S栈则晋升，移动到栈顶和Q队列尾
 * - **HIR非驻留**：缓存未命中，从S栈移除
 * - **不存在**：缓存未命中
 * 
 * @param cache 缓存指针
 * @param key 查询键
 * @return 值指针，未命中返回NULL
 * 
 * @note 会更新统计计数（hits/misses）
 * @note 可能触发晋升/降级/淘汰操作
 */
void* lirs_get(struct lirs_cache* cache, void* key);

/**
 * @brief 插入键值对到缓存
 * 
 * 核心插入操作，新块初始为HIR驻留：
 * 
 * - 若键已存在：更新值（不改变类型）
 * - 若S栈空间充足：直接设为LIR
 * - 否则：设为HIR驻留，放入Q队列
 * 
 * 空间不足时：
 * - 淘汰Q队列头部HIR块
 * - 清理S栈底部HIR非驻留块
 * - 若仍不足：插入失败
 * 
 * @param cache 缓存指针
 * @param key 键
 * @param value 值
 * @param size 块大小（字节）
 * @return 0 成功
 * @return -1 无效参数
 * @return -2 缓存空间不足
 * @return -3 内存分配失败
 * 
 * @note 会触发淘汰操作（空间不足时）
 */
int lirs_put(struct lirs_cache* cache, void* key, void* value, size_t size);

/**
 * @brief 从缓存移除键值对
 * 
 * 手动移除块（不等待淘汰）：
 * - 从S栈移除（若存在）
 * - 从Q队列移除（若存在）
 * - 从哈希表移除
 * - 销毁块（触发回调）
 * 
 * @param cache 缓存指针
 * @param key 键
 * @return 0 成功
 * @return -1 无效参数
 * @return -2 键不存在
 */
int lirs_remove(struct lirs_cache* cache, void* key);

/**
 * @brief 检查键是否存在（且驻留）
 * 
 * 快速存在性检查，不更新访问历史。
 * 
 * @param cache 缓存指针
 * @param key 键
 * @return true 存在且驻留（LIR或HIR驻留）
 * @return false 不存在或HIR非驻留
 */
bool lirs_contains(struct lirs_cache* cache, void* key);

/**
 * @brief 获取缓存当前大小（驻留块数量）
 * 
 * = LIR数量 + HIR驻留数量
 * 
 * @param cache 缓存指针
 * @return 驻留块数量
 */
size_t lirs_size(struct lirs_cache* cache);

/**
 * @brief 检查缓存是否为空
 * 
 * @param cache 缓存指针
 * @return true 空
 * @return false 非空
 */
bool lirs_is_empty(struct lirs_cache* cache);

/**
 * @brief 设置淘汰回调函数
 * 
 * 当块被淘汰或手动移除时调用回调。
 * 
 * @param cache 缓存指针
 * @param callback 回调函数
 * @param user_data 用户数据（传递给回调）
 */
void lirs_set_evict_callback(struct lirs_cache* cache, 
                             void (*callback)(void* key, void* value, size_t size),
                             void* user_data);

/**
 * @brief 清空缓存
 * 
 * 移除所有块，重置统计，触发所有回调。
 * 
 * @param cache 缓存指针
 */
void lirs_clear(struct lirs_cache* cache);

/**
 * @brief 动态调整缓存容量
 * 
 * 缩小时会降级/淘汰多余块。
 * 
 * @param cache 缓存指针
 * @param new_capacity 新容量
 */
void lirs_resize(struct lirs_cache* cache, size_t new_capacity);

/**
 * @brief 打印统计信息（调试用）
 * 
 * 输出到stdout，包含命中率、晋升/降级次数等。
 * 
 * @param cache 缓存指针
 */
void lirs_print_stats(struct lirs_cache* cache);

/**
 * @brief 获取统计信息结构体
 * 
 * @param cache 缓存指针
 * @param stats 输出统计结构体
 * @return 0 成功，-1 无效参数
 */
int lirs_get_stats(struct lirs_cache* cache, struct lirs_stats* stats);

/** @} */

/**
 * @defgroup LIRSStatsHelpers 统计辅助函数
 * @brief 快速获取关键统计指标
 * @{
 */

/**
 * @brief 获取命中次数
 */
static inline uint64_t lirs_get_hits(struct lirs_cache* cache) {
    return atomic64_load(&cache->stats.hits);
}

/**
 * @brief 获取未命中次数
 */
static inline uint64_t lirs_get_misses(struct lirs_cache* cache) {
    return atomic64_load(&cache->stats.misses);
}

/**
 * @brief 计算命中率
 * 
 * @return 0.0~1.0（0表示无访问，1表示全命中）
 */
static inline double lirs_get_hit_rate(struct lirs_cache* cache) {
    uint64_t hits = lirs_get_hits(cache);
    uint64_t misses = lirs_get_misses(cache);
    if (hits + misses == 0) return 0.0;
    return (double)hits / (double)(hits + misses);
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif