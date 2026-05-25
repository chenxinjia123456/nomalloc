#ifndef NOMALLOC_CACHE_TCACHE_H
#define NOMALLOC_CACHE_TCACHE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <nomalloc/types.h>
#include "../utils/atomic.h"
#include "../utils/spinlock.h"
#include "../utils/list.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NUM_SIZE_CLASSES
#define NUM_SIZE_CLASSES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#endif

#ifndef MAX_THREADS
#define MAX_THREADS NOMALLOC_MAX_THREADS
#endif

#define TCACHE_BIN_CAPACITY_SMALL   32
#define TCACHE_BIN_CAPACITY_MEDIUM  16
#define TCACHE_BIN_CAPACITY_LARGE   4

#define TCACHE_GC_THRESHOLD 0.5

struct tcache_bin {
    void** objects;
    size_t count;
    size_t capacity;
    
    spinlock_t lock;
};

struct tcache {
    struct tcache_bin bins[NUM_SIZE_CLASSES];
    
    atomic64_t alloc_count;
    atomic64_t free_count;
    atomic64_t cache_hits;
    atomic64_t cache_misses;
    
    size_t current_capacity;
    size_t max_capacity;
    
    atomic8_t active;
    
    struct list_head list;
};

struct tcache_manager {
    struct tcache* tcaches[MAX_THREADS];
    size_t num_tcaches;
    
    spinlock_t lock;
};

extern struct tcache_manager g_tcache_manager;

int tcache_manager_init(void);
void tcache_manager_shutdown(void);

struct tcache* tcache_create(void);
void tcache_destroy(struct tcache* tcache);

void* tcache_alloc(struct tcache* tcache, size_t size);
bool tcache_free(struct tcache* tcache, void* ptr);

void tcache_fill(struct tcache* tcache, size_t size);
void tcache_flush(struct tcache* tcache, size_t size);

void tcache_adjust_capacity(struct tcache* tcache);

void tcache_print_stats(struct tcache* tcache);

static inline size_t tcache_get_hits(struct tcache* tcache) {
    return atomic64_load(&tcache->cache_hits);
}

static inline size_t tcache_get_misses(struct tcache* tcache) {
    return atomic64_load(&tcache->cache_misses);
}

static inline double tcache_get_hit_rate(struct tcache* tcache) {
    size_t hits = tcache_get_hits(tcache);
    size_t misses = tcache_get_misses(tcache);
    
    if (hits + misses == 0) return 0.0;
    return (double)hits / (double)(hits + misses);
}

#ifdef __cplusplus
}
#endif

#endif