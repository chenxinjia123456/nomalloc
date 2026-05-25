#include "lirs.h"
#include "../../utils/memory.h"
#include "../../utils/log.h"
#include "../../utils/math.h"
#include "../../utils/assert.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define LIRS_HASH_TABLE_SIZE 1024
#define LIRS_HASH_TABLE_MASK (LIRS_HASH_TABLE_SIZE - 1)

static inline uint32_t lirs_hash_key(void* key) {
    return hash32_ptr(key);
}

static inline size_t lirs_hash_index(void* key, size_t mask) {
    return lirs_hash_key(key) & mask;
}

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

static void lirs_block_destroy(struct lirs_block* block, struct lirs_cache* cache) {
    if (!block) return;
    
    if (cache->evict_callback && block->value) {
        cache->evict_callback(block->key, block->value, block->size);
    }
    
    free(block);
}

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

static void lirs_insert_hash(struct lirs_cache* cache, struct lirs_block* block) {
    size_t idx = lirs_hash_index(block->key, cache->hash_table_mask);
    hlist_add_head(&block->hash_node, &cache->hash_table[idx]);
}

static void lirs_remove_hash(struct lirs_cache* cache, struct lirs_block* block) {
    hlist_del(&block->hash_node);
}

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