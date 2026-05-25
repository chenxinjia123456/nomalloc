#include "arena.h"
#include "../core/size_class.h"
#include "../memory/chunk.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include "../os/arch/prefetch.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void* __libc_malloc(size_t);
extern void __libc_free(void*);

#ifndef NUM_SIZE_CLASSES
#define NUM_SIZE_CLASSES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#endif

#ifndef MAX_ARENA_CHUNKS
#define MAX_ARENA_CHUNKS 1024
#endif

struct arena_manager g_arena_manager;

int arena_manager_init(void) {
    memset(&g_arena_manager, 0, sizeof(g_arena_manager));
    g_arena_manager.num_arenas = 0;
    g_arena_manager.numa_aware = 1;
    
    mutex_init(&g_arena_manager.lock);
    
    atomic64_init(&g_arena_manager.total_allocated, 0);
    atomic64_init(&g_arena_manager.total_freed, 0);
    
    return 0;
}

void arena_manager_shutdown(void) {
    mutex_lock(&g_arena_manager.lock);
    
    for (int i = 0; i < g_arena_manager.num_arenas; i++) {
        if (g_arena_manager.arenas[i]) {
            arena_destroy(g_arena_manager.arenas[i]);
            g_arena_manager.arenas[i] = NULL;
        }
    }
    
    g_arena_manager.num_arenas = 0;
    g_arena_manager.numa_aware = 1;
    
    atomic64_store(&g_arena_manager.total_allocated, 0);
    atomic64_store(&g_arena_manager.total_freed, 0);
    
    mutex_unlock(&g_arena_manager.lock);
    mutex_destroy(&g_arena_manager.lock);
    
    memset(&g_arena_manager, 0, sizeof(g_arena_manager));
}

struct arena* arena_create(int numa_node) {
    mutex_lock(&g_arena_manager.lock);
    
    if (g_arena_manager.num_arenas >= MAX_ARENAS) {
        mutex_unlock(&g_arena_manager.lock);
        return NULL;
    }
    
    struct arena* arena = (struct arena*)__libc_malloc(sizeof(struct arena));
    if (!arena) {
        mutex_unlock(&g_arena_manager.lock);
        return NULL;
    }
    
    arena->id = g_arena_manager.num_arenas;
    arena->numa_node = numa_node;
    
    arena->chunks = NULL;
    arena->num_chunks = 0;
    arena->max_chunks = MAX_ARENA_CHUNKS;
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        INIT_LIST_HEAD(&arena->chunks_free[i]);
        INIT_LIST_HEAD(&arena->chunks_full[i]);
        INIT_LIST_HEAD(&arena->chunks_partial[i]);
    }
    
    mutex_init(&arena->lock);
    spinlock_init(&arena->fast_lock);
    
    atomic64_init(&arena->allocated_bytes, 0);
    atomic64_init(&arena->freed_bytes, 0);
    atomic64_init(&arena->alloc_ops, 0);
    atomic64_init(&arena->free_ops, 0);
    atomic64_init(&arena->tcache_hits, 0);
    atomic64_init(&arena->tcache_misses, 0);
    atomic8_init(&arena->active, 1);
    
    INIT_LIST_HEAD(&arena->list);
    
    g_arena_manager.arenas[g_arena_manager.num_arenas++] = arena;
    
    mutex_unlock(&g_arena_manager.lock);
    
    return arena;
}

void arena_destroy(struct arena* arena) {
    if (!arena) return;
    
    int arena_id = arena->id;
    
    atomic8_store(&arena->active, 0);
    
    mutex_lock(&arena->lock);
    
    struct chunk* chunk;
    struct chunk* next;
    list_for_each_entry_safe(chunk, next, &arena->list, list) {
        list_del(&chunk->list);
        chunk_destroy(chunk);
    }
    
    arena->num_chunks = 0;
    
    mutex_unlock(&arena->lock);
    mutex_destroy(&arena->lock);
    
    if (arena_id < MAX_ARENAS) {
        g_arena_manager.arenas[arena_id] = NULL;
        if (arena_id == g_arena_manager.num_arenas - 1) {
            g_arena_manager.num_arenas--;
        }
    }
    
    __libc_free(arena);
}

struct arena* arena_get_current(void) {
    if (g_arena_manager.num_arenas == 0) {
        return NULL;
    }
    
    static __thread int thread_arena_id = -1;
    
    if (thread_arena_id >= 0 && thread_arena_id < g_arena_manager.num_arenas) {
        return g_arena_manager.arenas[thread_arena_id];
    }
    
    thread_arena_id = 0;
    return g_arena_manager.arenas[0];
}

struct arena* arena_get_by_id(int id) {
    if (id < 0 || id >= g_arena_manager.num_arenas) {
        return NULL;
    }
    return g_arena_manager.arenas[id];
}

struct arena* arena_get_by_numa_node(int numa_node) {
    for (int i = 0; i < g_arena_manager.num_arenas; i++) {
        if (g_arena_manager.arenas[i]->numa_node == numa_node) {
            return g_arena_manager.arenas[i];
        }
    }
    return NULL;
}

struct chunk* arena_find_available_chunk(struct arena* arena, size_t size_class) {
    if (!list_empty(&arena->chunks_partial[size_class])) {
        return list_first_entry(&arena->chunks_partial[size_class], struct chunk, list);
    }
    
    if (!list_empty(&arena->chunks_free[size_class])) {
        return list_first_entry(&arena->chunks_free[size_class], struct chunk, list);
    }
    
    return NULL;
}

void* arena_malloc(struct arena* arena, size_t size) {
    if (!arena || size == 0) {
        return NULL;
    }
    
    if (!atomic8_load(&arena->active)) {
        return NULL;
    }
    
    size_t class_idx = size_to_class(size);
    
    atomic64_inc(&arena->alloc_ops);
    
    if (is_large_class_idx(class_idx)) {
        mutex_lock(&arena->lock);
        
        if (arena->num_chunks >= arena->max_chunks) {
            mutex_unlock(&arena->lock);
            log_error("Arena %d: maximum chunks reached", arena->id);
            return NULL;
        }
        
        struct chunk* chunk = chunk_create(CHUNK_DEFAULT_SIZE, arena);
        if (!chunk) {
            mutex_unlock(&arena->lock);
            return NULL;
        }
        
        void* ptr = chunk_alloc_large(chunk, size);
        if (!ptr) {
            chunk_destroy(chunk);
            mutex_unlock(&arena->lock);
            return NULL;
        }
        
        list_add_tail(&chunk->list, &arena->list);
        arena->num_chunks++;
        
        atomic64_add_fetch(&arena->allocated_bytes, size);
        atomic64_add_fetch(&g_arena_manager.total_allocated, size);
        
        mutex_unlock(&arena->lock);
        
        log_debug("Arena %d: allocated large object %p size %zu", arena->id, ptr, size);
        return ptr;
    }
    
    spinlock_lock(&arena->fast_lock);
    
    struct chunk* chunk = arena_find_available_chunk(arena, class_idx);
    
    if (!chunk) {
        spinlock_unlock(&arena->fast_lock);
        
        mutex_lock(&arena->lock);
        
        if (arena->num_chunks >= arena->max_chunks) {
            mutex_unlock(&arena->lock);
            log_error("Arena %d: maximum chunks reached", arena->id);
            return NULL;
        }
        
        chunk = chunk_create(CHUNK_DEFAULT_SIZE, arena);
        if (!chunk) {
            mutex_unlock(&arena->lock);
            return NULL;
        }
        
        list_add_tail(&chunk->list, &arena->list);
        arena->num_chunks++;
        
        list_add_tail(&chunk->list, &arena->chunks_free[class_idx]);
        
        mutex_unlock(&arena->lock);
        
        spinlock_lock(&arena->fast_lock);
    }
    
    size_t alloc_size = class_to_size(class_idx);
    void* ptr = chunk_alloc_small(chunk, class_idx);
    
    if (ptr) {
        atomic64_add_fetch(&arena->allocated_bytes, alloc_size);
        atomic64_add_fetch(&g_arena_manager.total_allocated, alloc_size);
        
        prefetch_l1(ptr);
    }
    
    spinlock_unlock(&arena->fast_lock);
    
    log_trace("Arena %d: allocated %p size_class %zu size %zu", 
              arena->id, ptr, class_idx, alloc_size);
    return ptr;
}

void arena_free(struct arena* arena, void* ptr) {
    if (!arena || !ptr) {
        return;
    }
    
    if (!atomic8_load(&arena->active)) {
        return;
    }
    
    struct chunk* chunk = ptr_to_chunk(ptr);
    
    if (!ptr_is_in_chunk(chunk, ptr)) {
        log_warn("Arena %d: ptr %p not in any chunk", arena->id, ptr);
        return;
    }
    
    atomic64_inc(&arena->free_ops);
    
    size_t size = 0;
    
    if (chunk->is_large) {
        mutex_lock(&arena->lock);
        
        size = atomic64_load(&chunk->allocated_bytes) - 
               atomic64_load(&chunk->freed_bytes);
        
        chunk_free_large(chunk, ptr, size);
        
        atomic64_add_fetch(&arena->freed_bytes, size);
        atomic64_add_fetch(&g_arena_manager.total_freed, size);
        
        if (chunk_get_utilization(chunk) < 0.1) {
            list_del(&chunk->list);
            chunk_destroy(chunk);
            arena->num_chunks--;
        }
        
        mutex_unlock(&arena->lock);
    } else {
        spinlock_lock(&arena->fast_lock);
        
        uintptr_t addr = (uintptr_t)ptr;
        uintptr_t usable_base = (uintptr_t)chunk->base_addr + chunk->usable_offset;
        size_t offset = addr - usable_base;
        
        size_t class_idx = 0;
        size_t size = 0;
        for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
            size_t block_size = class_to_size(i);
            if (block_size == 0) continue;
            if (offset % block_size == 0 && offset < chunk->size - chunk->usable_offset) {
                class_idx = i;
                size = block_size;
                break;
            }
        }
        
        chunk_free_small(chunk, ptr, class_idx);
        
        atomic64_add_fetch(&arena->freed_bytes, size);
        atomic64_add_fetch(&g_arena_manager.total_freed, size);
        
        spinlock_unlock(&arena->fast_lock);
    }
    
    log_trace("Arena %d: freed %p size %zu", arena->id, ptr, size);
}

void* arena_realloc(struct arena* arena, void* ptr, size_t size) {
    if (!arena) {
        return NULL;
    }
    
    if (!ptr) {
        return arena_malloc(arena, size);
    }
    
    if (size == 0) {
        arena_free(arena, ptr);
        return NULL;
    }
    
    struct chunk* chunk = ptr_to_chunk(ptr);
    size_t old_size;
    
    if (chunk->is_large) {
        old_size = atomic64_load(&chunk->allocated_bytes) - 
                   atomic64_load(&chunk->freed_bytes);
    } else {
        uintptr_t addr = (uintptr_t)ptr;
        uintptr_t chunk_base = (uintptr_t)chunk->base_addr;
        size_t offset = addr - chunk_base;
        
        for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
            size_t block_size = class_to_size(i);
            if (offset % block_size == 0 && offset < chunk->size) {
                old_size = block_size;
                break;
            }
        }
    }
    
    if (size <= old_size) {
        return ptr;
    }
    
    void* new_ptr = arena_malloc(arena, size);
    if (!new_ptr) {
        return NULL;
    }
    
    memcpy(new_ptr, ptr, old_size);
    arena_free(arena, ptr);
    
    log_debug("Arena %d: realloc %p -> %p, old_size=%zu, new_size=%zu",
              arena->id, ptr, new_ptr, old_size, size);
    return new_ptr;
}

void arena_print_stats(struct arena* arena) {
    printf("Arena %d Statistics:\n", arena->id);
    printf("  NUMA node: %d\n", arena->numa_node);
    printf("  Active: %s\n", atomic8_load(&arena->active) ? "yes" : "no");
    printf("  Chunks: %zu\n", arena->num_chunks);
    printf("  Allocated: %llu bytes\n", atomic64_load(&arena->allocated_bytes));
    printf("  Freed: %llu bytes\n", atomic64_load(&arena->freed_bytes));
    printf("  Active allocations: %llu bytes\n", arena_get_active(arena));
    printf("  Alloc operations: %llu\n", atomic64_load(&arena->alloc_ops));
    printf("  Free operations: %llu\n", atomic64_load(&arena->free_ops));
    printf("  Tcache hits: %llu\n", atomic64_load(&arena->tcache_hits));
    printf("  Tcache misses: %llu\n", atomic64_load(&arena->tcache_misses));
}

void arena_manager_print_stats(void) {
    printf("Arena Manager Statistics:\n");
    printf("  Total arenas: %d\n", g_arena_manager.num_arenas);
    printf("  NUMA aware: %s\n", g_arena_manager.numa_aware ? "yes" : "no");
    printf("  Total allocated: %llu bytes\n", 
           atomic64_load(&g_arena_manager.total_allocated));
    printf("  Total freed: %llu bytes\n", 
           atomic64_load(&g_arena_manager.total_freed));
    printf("  Total active: %llu bytes\n",
           atomic64_load(&g_arena_manager.total_allocated) - 
           atomic64_load(&g_arena_manager.total_freed));
    
    for (int i = 0; i < g_arena_manager.num_arenas; i++) {
        printf("\n");
        arena_print_stats(g_arena_manager.arenas[i]);
    }
}