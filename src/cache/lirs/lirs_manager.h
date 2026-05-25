#ifndef NOMALLOC_CACHE_LIRS_LIRS_MANAGER_H
#define NOMALLOC_CACHE_LIRS_LIRS_MANAGER_H

#include "lirs.h"
#include "../../core/size_class.h"
#include "../../utils/spinlock.h"
#include "../../utils/list.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NUM_SIZE_CLASSES
#define NUM_SIZE_CLASSES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#endif

struct lirs_bin {
    struct lirs_cache* cache;
    size_t size_class;
    spinlock_t lock;
};

struct lirs_manager {
    struct lirs_bin bins[NUM_SIZE_CLASSES];
    
    size_t total_capacity;
    size_t total_max_size;
    
    atomic64_t hits;
    atomic64_t misses;
    atomic64_t evictions;
    
    spinlock_t global_lock;
};

extern struct lirs_manager g_lirs_manager;

int lirs_manager_init(void);
void lirs_manager_shutdown(void);

int lirs_manager_configure(size_t total_capacity, size_t max_size);

void* lirs_manager_get(size_t size);
int lirs_manager_put(void* ptr, size_t size);
int lirs_manager_remove(void* ptr);

void lirs_manager_print_stats(void);
int lirs_manager_get_global_stats(struct lirs_stats* stats);

static inline double lirs_manager_get_hit_rate(void) {
    uint64_t hits = atomic64_load(&g_lirs_manager.hits);
    uint64_t misses = atomic64_load(&g_lirs_manager.misses);
    if (hits + misses == 0) return 0.0;
    return (double)hits / (double)(hits + misses);
}

#ifdef __cplusplus
}
#endif

#endif