#ifndef NOMALLOC_MEMORY_CHUNK_H
#define NOMALLOC_MEMORY_CHUNK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../utils/bitmap.h"
#include "../utils/list.h"
#include "../utils/spinlock.h"
#include "../utils/memory.h"
#include "../utils/assert.h"
#include "../utils/atomic.h"
#include "../core/size_class.h"

#ifdef __cplusplus
extern "C" {
#endif

struct arena;

#define CHUNK_DEFAULT_SIZE (2 * 1024 * 1024)
#define CHUNK_MIN_SIZE     (4 * 1024)
#define CHUNK_MAX_SIZE     (256 * 1024 * 1024)
#define CHUNK_ALIGN        CHUNK_DEFAULT_SIZE

#define CHUNK_BITMAP_BITS_PER_BLOCK 1
#define CHUNK_RUN_SIZE              (4 * 1024)

struct chunk_run {
    size_t size_class;
    size_t block_size;
    size_t num_blocks;
    
    bitmap_t alloc_bitmap;
    size_t bitmap_size;
    
    size_t free_blocks_count;
    
    struct list_head list;
};

struct chunk {
    void* base_addr;
    size_t size;
    size_t usable_offset;
    
    struct arena* arena;
    
    bool is_large;
    
    struct chunk_run* runs;
    size_t num_runs;
    
    struct list_head runs_free[NUM_SIZE_CLASSES];
    struct list_head runs_full[NUM_SIZE_CLASSES];
    
    spinlock_t lock;
    
    struct list_head list;
    
    atomic64_t allocated_bytes;
    atomic64_t freed_bytes;
};

struct chunk* chunk_create(size_t size, struct arena* arena);
void chunk_destroy(struct chunk* chunk);

void* chunk_alloc_small(struct chunk* chunk, size_t size_class);
void chunk_free_small(struct chunk* chunk, void* ptr, size_t size_class);

void* chunk_alloc_large(struct chunk* chunk, size_t size);
void chunk_free_large(struct chunk* chunk, void* ptr, size_t size);

static inline struct chunk* ptr_to_chunk(void* ptr) {
    return (struct chunk*)((uintptr_t)ptr & ~(CHUNK_ALIGN - 1));
}

static inline bool ptr_is_in_chunk(struct chunk* chunk, void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t chunk_start = (uintptr_t)chunk->base_addr;
    uintptr_t chunk_end = chunk_start + chunk->size;
    return addr >= chunk_start && addr < chunk_end;
}

static inline size_t chunk_get_available(struct chunk* chunk) {
    return chunk->size - atomic64_load(&chunk->allocated_bytes);
}

static inline double chunk_get_utilization(struct chunk* chunk) {
    uint64_t allocated = atomic64_load(&chunk->allocated_bytes);
    return (double)allocated / (double)chunk->size;
}

struct chunk_run* chunk_find_available_run(struct chunk* chunk, size_t size_class);

void chunk_print_stats(struct chunk* chunk);

#ifdef __cplusplus
}
#endif

#endif