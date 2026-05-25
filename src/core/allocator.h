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

#ifndef MAX_THREADS
#define MAX_THREADS NOMALLOC_MAX_THREADS
#endif

struct allocator {
    bool initialized;
    
    struct nomalloc_allocator_config config;
    
    struct tcache* tcaches[MAX_THREADS];
    pthread_key_t tcache_key;
    
    void* gc;
    
    void* stats;
    
    void* leak_detector;
    
    void* profiler;
    
    atomic64_t total_allocated;
    atomic64_t total_freed;
    
    mutex_t lock;
};

extern struct allocator g_allocator;

int allocator_init(void);
void allocator_shutdown(void);

void* allocator_malloc(size_t size);
void allocator_free(void* ptr);
void* allocator_calloc(size_t nmemb, size_t size);
void* allocator_realloc(void* ptr, size_t size);
void* allocator_aligned_alloc(size_t alignment, size_t size);
size_t allocator_malloc_usable_size(void* ptr);

int allocator_configure(const struct nomalloc_allocator_config* config);
void allocator_print_stats(void);

static inline bool allocator_is_initialized(void) {
    return g_allocator.initialized;
}

static inline size_t allocator_get_total_allocated(void) {
    return atomic64_load(&g_allocator.total_allocated);
}

static inline size_t allocator_get_total_freed(void) {
    return atomic64_load(&g_allocator.total_freed);
}

static inline size_t allocator_get_active_allocations(void) {
    return allocator_get_total_allocated() - allocator_get_total_freed();
}

#ifdef __cplusplus
}
#endif

#endif