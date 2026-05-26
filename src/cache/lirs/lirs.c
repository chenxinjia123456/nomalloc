/**
 * @file lirs.c
 * @brief LIRS缓存替换算法实现
 * 
 * 本文件实现LIRS算法的核心逻辑，包括：
 * - 块创建与销毁
 * - 哈希表操作
 * - S栈维护与清理
 * - Q队列淘汰
 * - LIR/HIR晋升与降级
 * - 缓存查询、插入、删除
 * 
 * ## 核心算法流程
 * 
 * ### 1. 缓存命中（lirs_get）
 * 
 * ```
 * 访问LIR块：
 * ┌──────────┐
 * │ LIR块命中 │
 * │ 移动到栈顶 │────► S栈顶
 * │ 清理栈底   │────► 移除HIR非驻留
 * └──────────┘
 * 
 * 访问HIR驻留块：
 * ┌──────────────┐
 * │ HIR驻留块命中 │
 * │ 若在S栈      │────► 晋升为LIR
 * │S栈底LIR降级   │────► 变为HIR驻留
 * │ 移动到栈顶    │────► S栈顶
 * │ 移动到Q尾     │────► Q队列尾
 * │ 淘汰Q头       │────► 若超出容量
 * └──────────────┘
 * ```
 * 
 * ### 2. 缓存插入（lirs_put）
 * 
 * ```
 * 新块插入：
 * ┌──────────────┐
 * │ 创建HIR块     │
 * │S栈空间充足？  │────► Yes: 设为LIR
 * │              │────► No:  设为HIR驻留
 * │ 加入S栈顶     │────► 记录访问历史
 * │ 加入Q队列尾   │────► 仅HIR驻留
 * │ 插入哈希表    │────► 快速查找
 * │ 清理/淘汰     │────► 维护容量
 * └──────────────┘
 * ```
 * 
 * ### 3. 栈底清理（lirs_prune_s_stack）
 * 
 * ```
 * S栈清理：
 * ┌────────────────────────────┐
 * │ 检查栈底元素               │
 * │ 若是LIR：停止清理           │
 * │ 若是HIR驻留：移出栈         │
 * │ 若是HIR非驻留：销毁         │
 * │ 重复直到栈底是LIR           │
 * └────────────────────────────┘
 * ```
 * 
 * ### 4. HIR淘汰（lirs_evict_hir）
 * 
 * ```
 * Q队列淘汰：
 * ┌────────────────────────────┐
 * │ Q队列超出hir_capacity？    │
 * │ 取队头HIR块                │
 * │ 从Q队列移除                │
 * │ 设为HIR非驻留              │
 * │ 若不在S栈：销毁            │
 * │ 若在S栈：保留历史          │
 * │ 更新统计                   │
 * └────────────────────────────┘
 * ```
 * 
 * ## 关键优化点
 * 
 * 1. **哈希表查找**：O(1)平均查找时间
 * 2. **栈底LIR标记**：快速确定清理边界
 * 3. **延迟销毁**：HIR非驻留块保留在S栈
 * 4. **批量清理**：一次性清理多个HIR非驻留块
 * 
 * @author chenxinjia123456
 * @date 2026
 */

#include "lirs.h"
#include "../../utils/memory.h"
#include "../../utils/log.h"
#include "../../utils/math.h"
#include "../../utils/assert.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @defgroup LIRSInternal LIRS内部辅助函数
 * @{
 */

/**
 * @def LIRS_HASH_TABLE_SIZE
 * @brief 默认哈希表大小
 * 
 * 实际大小根据capacity动态计算。
 */
#define LIRS_HASH_TABLE_SIZE 1024

/**
 * @def LIRS_HASH_TABLE_MASK
 * @brief 哈希表掩码（用于快速取模）
 */
#define LIRS_HASH_TABLE_MASK (LIRS_HASH_TABLE_SIZE - 1)

/**
 * @brief 计算键的哈希值
 * 
 * 使用指针哈希函数，将void*映射到uint32_t。
 * 
 * @param key 键指针
 * @return 哈希值
 */
static inline uint32_t lirs_hash_key(void* key) {
    return hash32_ptr(key);
}

/**
 * @brief 计算哈希表索引
 * 
 * 哈希值对掩码取模，得到哈希表桶索引。
 * 
 * @param key 键指针
 * @param mask 哈希表掩码
 * @return 桶索引（0 ~ mask）
 */
static inline size_t lirs_hash_index(void* key, size_t mask) {
    return lirs_hash_key(key) & mask;
}

/**
 * @defgroup LIRSBlockOps LIRS块操作
 * @brief 块的创建、销毁、查找、哈希操作
 * @{
 */

/**
 * @brief 创建新的LIRS块
 * 
 * 分配块结构体，初始化所有字段。
 * 新块默认为HIR驻留类型（后续可能晋升）。
 * 
 * 初始化内容：
 * - key/value/size: 用户数据
 * - type: LIRS_BLOCK_HIR_RESIDENT
 * - access_time/reuse_distance: 0（未访问）
 * - in_s_stack/in_q_queue: false（未加入栈/队列）
 * - 链表节点: 初始化为空
 * 
 * @param key 键
 * @param value 值
 * @param size 块大小
 * @return 块指针，失败返回NULL
 * 
 * @note 不立即加入栈/队列（由lirs_put负责）
 */
static struct lirs_block* lirs_block_create(void* key, void* value, size_t size) {
    struct lirs_block* block = (struct lirs_block*)malloc(sizeof(struct lirs_block));
    if (!block) {
        log_error("Failed to allocate LIRS block");
        return NULL;
    }
    
    block->key = key;
    block->value = value;
    block->size = size;
    block->type = LIRS_BLOCK_HIR_RESIDENT;
    block->access_time = 0;
    block->reuse_distance = 0;
    block->in_s_stack = false;
    block->in_q_queue = false;
    
    INIT_LIST_HEAD(&block->s_list);
    INIT_LIST_HEAD(&block->q_list);
    INIT_HLIST_NODE(&block->hash_node);
    
    return block;
}

/**
 * @brief 销毁LIRS块
 * 
 * 释放块资源，触发淘汰回调（若有）。
 * 
 * @param block 块指针
 * @param cache 缓存指针（用于回调）
 * 
 * @note 回调在块销毁前执行
 * @note 块必须已从链表和哈希表移除
 */
static void lirs_block_destroy(struct lirs_block* block, struct lirs_cache* cache) {
    if (!block) return;
    
    if (cache->evict_callback && block->value) {
        cache->evict_callback(block->key, block->value, block->size);
    }
    
    free(block);
}

/**
 * @brief 在哈希表中查找块
 * 
 * 根据键在哈希表桶中查找匹配的块。
 * 
 * 查找流程：
 * 1. 计算哈希索引
 * 2.遍历哈希桶链表
 * 3. 比较键指针
 * 
 * @param cache 缓存指针
 * @param key 键
 * @return 块指针，未找到返回NULL
 * 
 * @note O(1)平均时间复杂度
 */
static struct lirs_block* lirs_find_block(struct lirs_cache* cache, void* key) {
    size_t idx = lirs_hash_index(key, cache->hash_table_mask);
    
    struct lirs_block* block;
    hlist_for_each_entry(block, &cache->hash_table[idx], hash_node) {
        if (block->key == key) {
            return block;
        }
    }
    
    return NULL;
}

/**
 * @brief 将块插入哈希表
 * 
 * 添加到哈希桶头部（最新块优先）。
 * 
 * @param cache 缓存指针
 * @param block 块指针
 * 
 * @note 块的hash_node必须已初始化
 */
static void lirs_insert_hash(struct lirs_cache* cache, struct lirs_block* block) {
    size_t idx = lirs_hash_index(block->key, cache->hash_table_mask);
    hlist_add_head(&block->hash_node, &cache->hash_table[idx]);
}

/**
 * @brief 从哈希表移除块
 * 
 * 从哈希桶链表中移除块。
 * 
 * @param cache 缓存指针
 * @param block 块指针
 * 
 * @note 移除后块仍存在，仅断开哈希连接
 */
static void lirs_remove_hash(struct lirs_cache* cache, struct lirs_block* block) {
    hlist_del(&block->hash_node);
}

/** @} */

/**
 * @defgroup LIRSStackOps S栈操作
 * @brief S栈维护、清理、降级、晋升
 * @{
 */

/**
 * @brief 清理S栈底部
 * 
 * LIRS算法核心操作：从栈底移除HIR块。
 * 
 * 清理规则：
 * - 栈底是LIR：停止清理（LIR必须保留）
 * - 栈底是HIR驻留：移出栈（保留在Q队列）
 * - 栈底是HIR非驻留：销毁（已淘汰，无价值）
 * 
 * 清理目的：
 * - 保持栈底是LIR（作为降级候选）
 * - 移除无用的HIR非驻留块
 * - 控制栈大小（避免无限增长）
 * 
 * 清理流程示意：
 * ```
 * 清理前：                 清理后：
 * [LIR] ← 栈顶            [LIR] ← 栈顶
 * [HIR驻留]               [HIR驻留]
 * [HIR非驻留] ─销毁      [LIR] ← 栈底（停止）
 * [HIR非驻留] ─销毁
 * [LIR] ← 栈底
 * ```
 * 
 * @param cache 缓存指针
 * 
 * @note 会销毁HIR非驻留块
 * @note 不会改变Q队列
 */
static void lirs_prune_s_stack(struct lirs_cache* cache) {
    while (!list_empty(&cache->s_stack)) {
        struct lirs_block* bottom = list_last_entry(&cache->s_stack, 
                                                     struct lirs_block, s_list);
        
        if (bottom->type == LIRS_BLOCK_LIR && bottom->in_s_stack) {
            break;
        }
        
        list_del(&bottom->s_list);
        bottom->in_s_stack = false;
        cache->s_stack_size--;
        
        if (bottom->type == LIRS_BLOCK_HIR_NON_RESIDENT) {
            lirs_remove_hash(cache, bottom);
            lirs_block_destroy(bottom, cache);
        }
    }
}

/** @} */

/**
 * @defgroup LIRSQueueOps Q队列操作
 * @brief HIR块淘汰、晋升、降级
 * @{
 */

/**
 * @brief 淘汰Q队列中的HIR块
 * 
 * HIR块淘汰策略：
 * - 从Q队列头部取最老HIR
 * - 设为HIR非驻留（已淘汰）
 * - 若不在S栈：销毁（无历史价值）
 * - 若在S栈：保留（历史可能晋升）
 * 
 * 淘汰条件：
 * - Q队列大小超过hir_capacity
 * - 或缓存总大小超过max_size
 * 
 * 淘汰流程示意：
 * ```
 * Q队列：
 * [HIR老] ←淘汰────► HIR非驻留/销毁
 * [HIR]
 * [HIR]
 * [HIR新] ← 队尾
 * ```
 * 
 * @param cache 缓存指针
 * 
 * @note 会触发evict_callback
 * @note 更新统计计数（evictions, hir_resident_count）
 */
static void lirs_evict_hir(struct lirs_cache* cache) {
    while (!list_empty(&cache->q_queue) && 
           cache->q_queue_size > cache->hir_capacity) {
        struct lirs_block* hir_block = list_first_entry(&cache->q_queue,
                                                        struct lirs_block, q_list);
        
        list_del(&hir_block->q_list);
        hir_block->in_q_queue = false;
        hir_block->type = LIRS_BLOCK_HIR_NON_RESIDENT;
        cache->q_queue_size--;
        
        atomic64_dec(&cache->stats.hir_resident_count);
        atomic64_inc(&cache->stats.hir_non_resident_count);
        atomic64_sub_fetch(&cache->stats.total_size, hir_block->size);
        
        atomic64_inc(&cache->stats.evictions);
        
        if (!hir_block->in_s_stack) {
            lirs_remove_hash(cache, hir_block);
            lirs_block_destroy(hir_block, cache);
        }
    }
}

/**
 * @brief 降级LIR块为HIR驻留块
 * 
 * 当HIR块晋升时，栈底LIR需要降级：
 * - 类型改为HIR驻留
 * - 加入Q队列尾部
 * - 不从S栈移除（保留历史）
 * 
 * 降级目的：
 * - 为新晋升的HIR腾出LIR位置
 * - 保证LIR数量不超过容量限制
 * 
 * 降级流程：
 * ```
 * 栈底LIR降级：
 * [LIR] ←降级────► [HIR驻留]
 *               加入Q队列尾
 * ```
 * 
 * @param cache 缓存指针
 * @param lir_block 要降级的LIR块
 * 
 * @note 更新统计计数（lir_count, hir_resident_count, demotions）
 */
static void lirs_demote_lir(struct lirs_cache* cache, struct lirs_block* lir_block) {
    lir_block->type = LIRS_BLOCK_HIR_RESIDENT;
    atomic64_dec(&cache->stats.lir_count);
    atomic64_inc(&cache->stats.hir_resident_count);
    atomic64_inc(&cache->stats.demotions);
    
    if (!lir_block->in_q_queue) {
        list_add_tail(&lir_block->q_list, &cache->q_queue);
        lir_block->in_q_queue = true;
        cache->q_queue_size++;
    }
}

/**
 * @brief 晋升HIR块为LIR块
 * 
 * 当HIR块在S栈中被访问时晋升：
 * - 类型改为LIR
 * - 从Q队列移除
 * - 栈底LIR降级
 * - 清理栈底
 * - 淘汰多余HIR
 * 
 * 晋升条件：
 * - HIR驻留块被访问
 * - 该块在S栈中（有访问历史）
 * 
 * 晋升流程示意：
 * ```
 * S栈：
 * [HIR命中] ←晋升────► [LIR]
 * [其他块]               [其他块]
 * [LIR栈底] ←降级────► [HIR驻留]
 * ```
 * 
 * @param cache 缓存指针
 * @param hir_block 要晋升的HIR块
 * 
 * @note 会触发降级操作（栈底LIR）
 * @note 会触发淘汰操作（若HIR过多）
 */
static void lirs_promote_hir(struct lirs_cache* cache, struct lirs_block* hir_block) {
    hir_block->type = LIRS_BLOCK_LIR;
    atomic64_inc(&cache->stats.lir_count);
    atomic64_dec(&cache->stats.hir_resident_count);
    atomic64_inc(&cache->stats.promotions);
    
    if (hir_block->in_q_queue) {
        list_del(&hir_block->q_list);
        hir_block->in_q_queue = false;
        cache->q_queue_size--;
    }
    
    struct lirs_block* bottom = list_last_entry(&cache->s_stack,
                                                struct lirs_block, s_list);
    if (bottom && bottom->type == LIRS_BLOCK_LIR) {
        lirs_demote_lir(cache, bottom);
        list_del(&bottom->s_list);
        bottom->in_s_stack = false;
        cache->s_stack_size--;
    }
    
    lirs_prune_s_stack(cache);
    lirs_evict_hir(cache);
}

/** @} */

/**
 * @defgroup LIRSCoreOps LIRS核心操作
 * @brief 缓存创建、销毁、查询、插入
 * @{
 */

/**
 * @brief 创建LIRS缓存
 * 
 * 初始化完整缓存结构：
 * 
 * 1. **分配缓存结构**
 *    - 分配lirs_cache结构体
 *    - 设置容量参数
 * 
 * 2. **初始化链表**
 *    - S栈：INIT_LIST_HEAD
 *    - Q队列：INIT_LIST_HEAD
 * 
 * 3. **创建哈希表**
 *    - 大小：capacity * 2（减少冲突）
 *    - 每桶初始化为空链表
 * 
 * 4. **初始化统计**
 *    - 所有计数器置零
 *    - max_size设置
 * 
 * 5. **初始化锁**
 *    - spinlock用于并发保护
 * 
 * 内存布局：
 * ```
 * lirs_cache结构：
 * ┌─────────────────────────────────┐
 * │ capacity, hir_capacity, max_size│ 容量配置
 * │ s_stack, q_queue                │ 链表头
 * │ hash_table[hash_size]           │ 哈希桶数组
 * │ lock                            │ 自旋锁
 * │ stats                           │ 统计原子变量
 * │ evict_callback, user_data       │ 回调配置
 * └─────────────────────────────────┘
 * ```
 * 
 * @param capacity 块数量容量
 * @param max_size 最大缓存大小（字节）
 * @return 缓存指针，失败返回NULL
 */
struct lirs_cache* lirs_create(size_t capacity, size_t max_size) {
    struct lirs_cache* cache = (struct lirs_cache*)malloc(sizeof(struct lirs_cache));
    if (!cache) {
        log_error("Failed to allocate LIRS cache");
        return NULL;
    }
    
    cache->capacity = capacity;
    cache->hir_capacity = max((size_t)(capacity * LIRS_HIR_RATIO), LIRS_MIN_HIR_BLOCKS);
    cache->max_size = max_size;
    
    INIT_LIST_HEAD(&cache->s_stack);
    INIT_LIST_HEAD(&cache->q_queue);
    
    cache->s_stack_size = 0;
    cache->q_queue_size = 0;
    
    size_t hash_size = 1;
    while (hash_size < capacity * 2) {
        hash_size *= 2;
    }
    
    cache->hash_table = (struct hlist_head*)calloc(hash_size, sizeof(struct hlist_head));
    if (!cache->hash_table) {
        free(cache);
        log_error("Failed to allocate LIRS hash table");
        return NULL;
    }
    
    cache->hash_table_size = hash_size;
    cache->hash_table_mask = hash_size - 1;
    
    for (size_t i = 0; i < hash_size; i++) {
        INIT_HLIST_HEAD(&cache->hash_table[i]);
    }
    
    spinlock_init(&cache->lock);
    
    atomic64_init(&cache->stats.hits, 0);
    atomic64_init(&cache->stats.misses, 0);
    atomic64_init(&cache->stats.evictions, 0);
    atomic64_init(&cache->stats.promotions, 0);
    atomic64_init(&cache->stats.demotions, 0);
    atomic64_init(&cache->stats.lir_count, 0);
    atomic64_init(&cache->stats.hir_resident_count, 0);
    atomic64_init(&cache->stats.hir_non_resident_count, 0);
    atomic64_init(&cache->stats.total_size, 0);
    atomic64_init(&cache->stats.max_size, max_size);
    
    cache->current_time = 0;
    cache->evict_callback = NULL;
    cache->user_data = NULL;
    
    log_debug("Created LIRS cache: capacity=%zu, hir_capacity=%zu, max_size=%zu",
              capacity, cache->hir_capacity, max_size);
    
    return cache;
}

/**
 * @brief销毁LIRS缓存
 * 
 * 完整清理流程：
 * 
 * 1. **持锁保护**
 *    - 获取spinlock防止并发
 * 
 * 2. **清理S栈**
 *    - 遍历所有栈中块
 *    - 从栈和哈希表移除
 *    - 销毁块（触发回调）
 * 
 * 3. **清理Q队列**
 *    - 遍历队列中块
 *    - 若不在栈：销毁
 *    - 若在栈：已由栈清理处理
 * 
 * 4. **释放资源**
 *    - 释放哈希表数组
 *    - 释放缓存结构体
 * 
 * @param cache 缓存指针
 * 
 * @note 所有块都会触发evict_callback
 */
void lirs_destroy(struct lirs_cache* cache) {
    if (!cache) return;
    
    spinlock_lock(&cache->lock);
    
    struct lirs_block* block;
    struct lirs_block* next;
    
    list_for_each_entry_safe(block, next, &cache->s_stack, s_list) {
        list_del(&block->s_list);
        lirs_remove_hash(cache, block);
        lirs_block_destroy(block, cache);
    }
    
    list_for_each_entry_safe(block, next, &cache->q_queue, q_list) {
        list_del(&block->q_list);
        if (!block->in_s_stack) {
            lirs_remove_hash(cache, block);
            lirs_block_destroy(block, cache);
        }
    }
    
    free(cache->hash_table);
    
    spinlock_unlock(&cache->lock);
    
    log_debug("Destroyed LIRS cache");
    free(cache);
}

/**
 * @brief 从缓存获取值（核心查询操作）
 * 
 * LIRS算法的核心逻辑，处理三种块类型的访问：
 * 
 * **1. LIR块命中**
 * - 更新访问时间
 * - 移动到S栈顶（最新访问）
 * - 清理栈底HIR非驻留块
 * - 返回value
 * 
 * **2. HIR驻留块命中**
 * - 更新访问时间
 * - 若在S栈中：晋升为LIR（触发降级）
 * - 移动到S栈顶
 * - 移动到Q队列尾（若仍是HIR）
 * - 淘汰多余HIR（若超出容量）
 * - 返回value
 * 
 * **3. HIR非驻留块命中**
 * - 缓存未命中（数据已淘汰）
 * - 若在S栈：从栈移除并销毁
 * - 清理栈底
 * - 返回NULL（需要重新加载）
 * 
 * **4. 块不存在**
 * - 缓存未命中
 * - 返回NULL
 * 
 * 查询流程示意：
 * ```
 * lirs_get(key):
 * ├─找到块？
 * │ ├─ Yes → 根据类型处理
 * │ │   ├─ LIR: 移栈顶+清理
 * │ │   ├─ HIR驻留: 晋升+移栈顶+移Q尾
 * │ │   ├─ HIR非驻留: 移除+返回NULL
 * │ │   →返回value
 * │ ├─ No → 返回NULL（未命中）
 * ```
 * 
 * @param cache 缓存指针
 * @param key 查询键
 * @return 值指针，未命中返回NULL
 * 
 * @note 会更新统计（hits/misses）
 * @note 可能触发晋升/降级/淘汰
 */
void* lirs_get(struct lirs_cache* cache, void* key) {
    if (!cache || !key) return NULL;
    
    spinlock_lock(&cache->lock);
    
    struct lirs_block* block = lirs_find_block(cache, key);
    
    if (!block) {
        atomic64_inc(&cache->stats.misses);
        spinlock_unlock(&cache->lock);
        log_trace("LIRS cache miss: key=%p", key);
        return NULL;
    }
    
    cache->current_time++;
    block->access_time = cache->current_time;
    
    switch (block->type) {
        case LIRS_BLOCK_LIR:
            atomic64_inc(&cache->stats.hits);
            
            if (block->in_s_stack) {
                list_del(&block->s_list);
                list_add(&block->s_list, &cache->s_stack);
            } else {
                list_add(&block->s_list, &cache->s_stack);
                block->in_s_stack = true;
                cache->s_stack_size++;
            }
            
            lirs_prune_s_stack(cache);
            break;
            
        case LIRS_BLOCK_HIR_RESIDENT:
            atomic64_inc(&cache->stats.hits);
            
            if (block->in_s_stack) {
                lirs_promote_hir(cache, block);
            }
            
            if (block->in_s_stack) {
                list_del(&block->s_list);
                list_add(&block->s_list, &cache->s_stack);
            } else {
                list_add(&block->s_list, &cache->s_stack);
                block->in_s_stack = true;
                cache->s_stack_size++;
            }
            
            if (block->type == LIRS_BLOCK_HIR_RESIDENT && !block->in_q_queue) {
                list_add_tail(&block->q_list, &cache->q_queue);
                block->in_q_queue = true;
                cache->q_queue_size++;
            }
            
            lirs_evict_hir(cache);
            break;
            
        case LIRS_BLOCK_HIR_NON_RESIDENT:
            atomic64_inc(&cache->stats.misses);
            
            if (block->in_s_stack) {
                lirs_remove_hash(cache, block);
                list_del(&block->s_list);
                block->in_s_stack = false;
                cache->s_stack_size--;
                lirs_block_destroy(block, cache);
                lirs_prune_s_stack(cache);
            }
            
            spinlock_unlock(&cache->lock);
            log_trace("LIRS cache miss (non-resident): key=%p", key);
            return NULL;
    }
    
    void* value = block->value;
    spinlock_unlock(&cache->lock);
    
    log_trace("LIRS cache hit: key=%p, type=%d, hit_rate=%.2f",
              key, block->type, lirs_get_hit_rate(cache));
    
    return value;
}

int lirs_put(struct lirs_cache* cache, void* key, void* value, size_t size) {
    if (!cache || !key) return -1;
    
    spinlock_lock(&cache->lock);
    
    struct lirs_block* existing = lirs_find_block(cache, key);
    
    if (existing) {
        if (existing->value != value) {
            if (cache->evict_callback && existing->value) {
                cache->evict_callback(existing->key, existing->value, existing->size);
            }
            
            atomic64_sub_fetch(&cache->stats.total_size, existing->size);
            atomic64_add_fetch(&cache->stats.total_size, size);
            existing->value = value;
            existing->size = size;
        }
        
        spinlock_unlock(&cache->lock);
        return 0;
    }
    
    while (atomic64_load(&cache->stats.total_size) + size > cache->max_size) {
        lirs_evict_hir(cache);
        
        if (atomic64_load(&cache->stats.total_size) + size > cache->max_size &&
            !list_empty(&cache->s_stack)) {
            lirs_prune_s_stack(cache);
        }
        
        if (atomic64_load(&cache->stats.total_size) + size > cache->max_size) {
            spinlock_unlock(&cache->lock);
            log_warn("LIRS cache full, cannot insert key=%p size=%zu", key, size);
            return -2;
        }
    }
    
    struct lirs_block* block = lirs_block_create(key, value, size);
    if (!block) {
        spinlock_unlock(&cache->lock);
        return -3;
    }
    
    cache->current_time++;
    block->access_time = cache->current_time;
    
    if (cache->s_stack_size < cache->capacity - cache->hir_capacity) {
        block->type = LIRS_BLOCK_LIR;
        atomic64_inc(&cache->stats.lir_count);
    } else {
        block->type = LIRS_BLOCK_HIR_RESIDENT;
        atomic64_inc(&cache->stats.hir_resident_count);
        
        list_add_tail(&block->q_list, &cache->q_queue);
        block->in_q_queue = true;
        cache->q_queue_size++;
    }
    
    list_add(&block->s_list, &cache->s_stack);
    block->in_s_stack = true;
    cache->s_stack_size++;
    
    lirs_insert_hash(cache, block);
    
    atomic64_add_fetch(&cache->stats.total_size, size);
    
    lirs_prune_s_stack(cache);
    lirs_evict_hir(cache);
    
    spinlock_unlock(&cache->lock);
    
    log_trace("LIRS cache insert: key=%p, size=%zu, type=%d",
              key, size, block->type);
    
    return 0;
}

int lirs_remove(struct lirs_cache* cache, void* key) {
    if (!cache || !key) return -1;
    
    spinlock_lock(&cache->lock);
    
    struct lirs_block* block = lirs_find_block(cache, key);
    if (!block) {
        spinlock_unlock(&cache->lock);
        return -2;
    }
    
    if (block->in_s_stack) {
        list_del(&block->s_list);
        block->in_s_stack = false;
        cache->s_stack_size--;
    }
    
    if (block->in_q_queue) {
        list_del(&block->q_list);
        block->in_q_queue = false;
        cache->q_queue_size--;
    }
    
    lirs_remove_hash(cache, block);
    
    atomic64_sub_fetch(&cache->stats.total_size, block->size);
    
    switch (block->type) {
        case LIRS_BLOCK_LIR:
            atomic64_dec(&cache->stats.lir_count);
            break;
        case LIRS_BLOCK_HIR_RESIDENT:
            atomic64_dec(&cache->stats.hir_resident_count);
            break;
        case LIRS_BLOCK_HIR_NON_RESIDENT:
            atomic64_dec(&cache->stats.hir_non_resident_count);
            break;
    }
    
    lirs_block_destroy(block, cache);
    
    lirs_prune_s_stack(cache);
    
    spinlock_unlock(&cache->lock);
    
    log_trace("LIRS cache remove: key=%p", key);
    
    return 0;
}

bool lirs_contains(struct lirs_cache* cache, void* key) {
    if (!cache || !key) return false;
    
    spinlock_lock(&cache->lock);
    struct lirs_block* block = lirs_find_block(cache, key);
    bool found = block != NULL && block->type != LIRS_BLOCK_HIR_NON_RESIDENT;
    spinlock_unlock(&cache->lock);
    
    return found;
}

size_t lirs_size(struct lirs_cache* cache) {
    if (!cache) return 0;
    return atomic64_load(&cache->stats.lir_count) + 
           atomic64_load(&cache->stats.hir_resident_count);
}

bool lirs_is_empty(struct lirs_cache* cache) {
    return lirs_size(cache) == 0;
}

void lirs_set_evict_callback(struct lirs_cache* cache,
                             void (*callback)(void* key, void* value, size_t size),
                             void* user_data) {
    if (!cache) return;
    
    spinlock_lock(&cache->lock);
    cache->evict_callback = callback;
    cache->user_data = user_data;
    spinlock_unlock(&cache->lock);
}

void lirs_clear(struct lirs_cache* cache) {
    if (!cache) return;
    
    spinlock_lock(&cache->lock);
    
    struct lirs_block* block;
    struct lirs_block* next;
    
    list_for_each_entry_safe(block, next, &cache->s_stack, s_list) {
        list_del(&block->s_list);
        lirs_remove_hash(cache, block);
        lirs_block_destroy(block, cache);
    }
    
    list_for_each_entry_safe(block, next, &cache->q_queue, q_list) {
        list_del(&block->q_list);
        if (!block->in_s_stack) {
            lirs_remove_hash(cache, block);
            lirs_block_destroy(block, cache);
        }
    }
    
    for (size_t i = 0; i < cache->hash_table_size; i++) {
        INIT_HLIST_HEAD(&cache->hash_table[i]);
    }
    
    INIT_LIST_HEAD(&cache->s_stack);
    INIT_LIST_HEAD(&cache->q_queue);
    
    cache->s_stack_size = 0;
    cache->q_queue_size = 0;
    
    atomic64_store(&cache->stats.lir_count, 0);
    atomic64_store(&cache->stats.hir_resident_count, 0);
    atomic64_store(&cache->stats.hir_non_resident_count, 0);
    atomic64_store(&cache->stats.total_size, 0);
    
    spinlock_unlock(&cache->lock);
    
    log_debug("LIRS cache cleared");
}

void lirs_resize(struct lirs_cache* cache, size_t new_capacity) {
    if (!cache) return;
    
    spinlock_lock(&cache->lock);
    
    cache->capacity = new_capacity;
    cache->hir_capacity = max((size_t)(new_capacity * LIRS_HIR_RATIO), 
                              LIRS_MIN_HIR_BLOCKS);
    
    while (atomic64_load(&cache->stats.lir_count) > 
           cache->capacity - cache->hir_capacity) {
        if (list_empty(&cache->s_stack)) break;
        
        struct lirs_block* bottom = list_last_entry(&cache->s_stack,
                                                    struct lirs_block, s_list);
        if (bottom->type == LIRS_BLOCK_LIR) {
            lirs_demote_lir(cache, bottom);
            list_del(&bottom->s_list);
            bottom->in_s_stack = false;
            cache->s_stack_size--;
        }
    }
    
    lirs_evict_hir(cache);
    
    spinlock_unlock(&cache->lock);
    
    log_debug("LIRS cache resized: new_capacity=%zu, hir_capacity=%zu",
              new_capacity, cache->hir_capacity);
}

void lirs_print_stats(struct lirs_cache* cache) {
    if (!cache) return;
    
    printf("\nLIRS Cache Statistics:\n");
    printf("  Capacity: %zu\n", cache->capacity);
    printf("  HIR capacity: %zu\n", cache->hir_capacity);
    printf("  Max size: %zu bytes\n", cache->max_size);
    printf("  Hits: %llu\n", lirs_get_hits(cache));
    printf("  Misses: %llu\n", lirs_get_misses(cache));
    printf("  Hit rate: %.2f%%\n", lirs_get_hit_rate(cache) * 100);
    printf("  Evictions: %llu\n", atomic64_load(&cache->stats.evictions));
    printf("  Promotions (HIR->LIR): %llu\n", atomic64_load(&cache->stats.promotions));
    printf("  Demotions (LIR->HIR): %llu\n", atomic64_load(&cache->stats.demotions));
    printf("  LIR blocks: %llu\n", atomic64_load(&cache->stats.lir_count));
    printf("  HIR resident blocks: %llu\n", atomic64_load(&cache->stats.hir_resident_count));
    printf("  HIR non-resident blocks: %llu\n", atomic64_load(&cache->stats.hir_non_resident_count));
    printf("  Current size: %llu bytes\n", atomic64_load(&cache->stats.total_size));
    printf("  S stack size: %zu\n", cache->s_stack_size);
    printf("  Q queue size: %zu\n", cache->q_queue_size);
}

int lirs_get_stats(struct lirs_cache* cache, struct lirs_stats* stats) {
    if (!cache || !stats) return -1;
    
    stats->hits = cache->stats.hits;
    stats->misses = cache->stats.misses;
    stats->evictions = cache->stats.evictions;
    stats->promotions = cache->stats.promotions;
    stats->demotions = cache->stats.demotions;
    stats->lir_count = cache->stats.lir_count;
    stats->hir_resident_count = cache->stats.hir_resident_count;
    stats->hir_non_resident_count = cache->stats.hir_non_resident_count;
    stats->total_size = cache->stats.total_size;
    stats->max_size = cache->stats.max_size;
    
    return 0;
}