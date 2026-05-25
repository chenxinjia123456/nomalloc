#ifndef NOMALLOC_MEMORY_ARENA_H
#define NOMALLOC_MEMORY_ARENA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <nomalloc/types.h>
#include "../utils/atomic.h"
#include "../utils/mutex.h"
#include "../utils/list.h"
#include "../utils/spinlock.h"
#include "../utils/log.h"
#include "../utils/assert.h"
#include "../memory/chunk.h"
#include "../core/size_class.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_ARENAS
#define MAX_ARENAS NOMALLOC_MAX_ARENAS
#endif

#ifndef MAX_ARENA_CHUNKS
#define MAX_ARENA_CHUNKS 1024
#endif

#ifndef NUM_SIZE_CLASSES
#define NUM_SIZE_CLASSES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#endif

struct arena {
    int id;
    int numa_node;
    
    struct chunk* chunks;
    size_t num_chunks;
    size_t max_chunks;
    
    struct list_head chunks_free[NUM_SIZE_CLASSES];
    struct list_head chunks_full[NUM_SIZE_CLASSES];
    struct list_head chunks_partial[NUM_SIZE_CLASSES];
    
    mutex_t lock;
    spinlock_t fast_lock;
    
    atomic64_t allocated_bytes;
    atomic64_t freed_bytes;
    atomic64_t alloc_ops;
    atomic64_t free_ops;
    
    atomic64_t tcache_hits;
    atomic64_t tcache_misses;
    
    atomic8_t active;
    
    struct list_head list;
};

struct arena_manager {
    struct arena* arenas[MAX_ARENAS];
    int num_arenas;
    int numa_aware;
    
    mutex_t lock;
    
    atomic64_t total_allocated;
    atomic64_t total_freed;
};

extern struct arena_manager g_arena_manager;

int arena_manager_init(void);
void arena_manager_shutdown(void);

struct arena* arena_create(int numa_node);
void arena_destroy(struct arena* arena);

struct arena* arena_get_current(void);
struct arena* arena_get_by_id(int id);
struct arena* arena_get_by_numa_node(int numa_node);

void* arena_malloc(struct arena* arena, size_t size);
void arena_free(struct arena* arena, void* ptr);
void* arena_realloc(struct arena* arena, void* ptr, size_t size);

static inline size_t arena_get_allocated(struct arena* arena) {
    return atomic64_load(&arena->allocated_bytes);
}

static inline size_t arena_get_freed(struct arena* arena) {
    return atomic64_load(&arena->freed_bytes);
}

static inline size_t arena_get_active(struct arena* arena) {
    return arena_get_allocated(arena) - arena_get_freed(arena);
}

static inline double arena_get_utilization(struct arena* arena) {
    size_t allocated = arena_get_allocated(arena);
    size_t total_capacity = 0;
    
    struct chunk* chunk;
    list_for_each_entry(chunk, &arena->list, list) {
        total_capacity += chunk->size;
    }
    
    if (total_capacity == 0) return 0.0;
    return (double)allocated / (double)total_capacity;
}

void arena_print_stats(struct arena* arena);
void arena_manager_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif