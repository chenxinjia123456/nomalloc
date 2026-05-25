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

#define LIRS_DEFAULT_CAPACITY 1024
#define LIRS_HIR_RATIO 0.01
#define LIRS_MIN_HIR_BLOCKS 2

typedef enum {
    LIRS_BLOCK_LIR,
    LIRS_BLOCK_HIR_RESIDENT,
    LIRS_BLOCK_HIR_NON_RESIDENT
} lirs_block_type_t;

struct lirs_block {
    void* key;
    void* value;
    size_t size;
    
    lirs_block_type_t type;
    
    uint64_t access_time;
    uint64_t reuse_distance;
    
    bool in_s_stack;
    bool in_q_queue;
    
    struct list_head s_list;
    struct list_head q_list;
    struct hlist_node hash_node;
};

struct lirs_stats {
    atomic64_t hits;
    atomic64_t misses;
    atomic64_t evictions;
    atomic64_t promotions;
    atomic64_t demotions;
    
    atomic64_t lir_count;
    atomic64_t hir_resident_count;
    atomic64_t hir_non_resident_count;
    
    atomic64_t total_size;
    atomic64_t max_size;
};

struct lirs_cache {
    size_t capacity;
    size_t hir_capacity;
    size_t max_size;
    
    struct list_head s_stack;
    struct list_head q_queue;
    
    size_t s_stack_size;
    size_t q_queue_size;
    
    struct hlist_head* hash_table;
    size_t hash_table_size;
    size_t hash_table_mask;
    
    spinlock_t lock;
    
    struct lirs_stats stats;
    
    uint64_t current_time;
    
    void (*evict_callback)(void* key, void* value, size_t size);
    void* user_data;
};

struct lirs_cache* lirs_create(size_t capacity, size_t max_size);
void lirs_destroy(struct lirs_cache* cache);

void* lirs_get(struct lirs_cache* cache, void* key);
int lirs_put(struct lirs_cache* cache, void* key, void* value, size_t size);
int lirs_remove(struct lirs_cache* cache, void* key);

bool lirs_contains(struct lirs_cache* cache, void* key);
size_t lirs_size(struct lirs_cache* cache);
bool lirs_is_empty(struct lirs_cache* cache);

void lirs_set_evict_callback(struct lirs_cache* cache, 
                             void (*callback)(void* key, void* value, size_t size),
                             void* user_data);

void lirs_clear(struct lirs_cache* cache);
void lirs_resize(struct lirs_cache* cache, size_t new_capacity);

void lirs_print_stats(struct lirs_cache* cache);
int lirs_get_stats(struct lirs_cache* cache, struct lirs_stats* stats);

static inline uint64_t lirs_get_hits(struct lirs_cache* cache) {
    return atomic64_load(&cache->stats.hits);
}

static inline uint64_t lirs_get_misses(struct lirs_cache* cache) {
    return atomic64_load(&cache->stats.misses);
}

static inline double lirs_get_hit_rate(struct lirs_cache* cache) {
    uint64_t hits = lirs_get_hits(cache);
    uint64_t misses = lirs_get_misses(cache);
    if (hits + misses == 0) return 0.0;
    return (double)hits / (double)(hits + misses);
}

#ifdef __cplusplus
}
#endif

#endif