#include "tcache.h"
#include "../core/size_class.h"
#include "../memory/arena.h"
#include "../memory/chunk.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include "../os/arch/prefetch.h"
#include <stdlib.h>
#include <string.h>

extern void* __libc_malloc(size_t);
extern void __libc_free(void*);

#ifndef NUM_SIZE_CLASSES
#define NUM_SIZE_CLASSES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#endif

#ifndef NOMALLOC_TCACHE_SIZE_MIN
#define NOMALLOC_TCACHE_SIZE_MIN (64 * 1024)
#endif

struct tcache_manager g_tcache_manager;

int tcache_manager_init(void) {
    memset(&g_tcache_manager, 0, sizeof(g_tcache_manager));
    g_tcache_manager.num_tcaches = 0;
    spinlock_init(&g_tcache_manager.lock);
    
    return 0;
}

void tcache_manager_shutdown(void) {
    spinlock_lock(&g_tcache_manager.lock);
    
    for (size_t i = 0; i < g_tcache_manager.num_tcaches; i++) {
        if (g_tcache_manager.tcaches[i]) {
            tcache_destroy(g_tcache_manager.tcaches[i]);
            g_tcache_manager.tcaches[i] = NULL;
        }
    }
    
    g_tcache_manager.num_tcaches = 0;
    
    spinlock_unlock(&g_tcache_manager.lock);
}

struct tcache* tcache_create(void) {
    struct tcache* tcache = (struct tcache*)__libc_malloc(sizeof(struct tcache));
    if (!tcache) {
        log_error("Failed to allocate tcache metadata");
        return NULL;
    }
    
    memset(tcache, 0, sizeof(struct tcache));
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        size_t capacity;
        if (is_small_class_idx(i)) {
            capacity = TCACHE_BIN_CAPACITY_SMALL;
        } else if (is_medium_class_idx(i)) {
            capacity = TCACHE_BIN_CAPACITY_MEDIUM;
        } else {
            capacity = TCACHE_BIN_CAPACITY_LARGE;
        }
        
        tcache->bins[i].objects = (void**)__libc_malloc(capacity * sizeof(void*));
        if (!tcache->bins[i].objects) {
            for (size_t j = 0; j < i; j++) {
                __libc_free(tcache->bins[j].objects);
            }
            __libc_free(tcache);
            log_error("Failed to allocate tcache bin objects");
            return NULL;
        }
        
        tcache->bins[i].count = 0;
        tcache->bins[i].capacity = capacity;
        spinlock_init(&tcache->bins[i].lock);
    }
    
    atomic64_init(&tcache->alloc_count, 0);
    atomic64_init(&tcache->free_count, 0);
    atomic64_init(&tcache->cache_hits, 0);
    atomic64_init(&tcache->cache_misses, 0);
    
    tcache->current_capacity = NOMALLOC_TCACHE_SIZE_DEFAULT;
    tcache->max_capacity = NOMALLOC_TCACHE_SIZE_DEFAULT;
    
    atomic8_init(&tcache->active, 1);
    INIT_LIST_HEAD(&tcache->list);
    
    log_debug("Created tcache");
    return tcache;
}

void tcache_destroy(struct tcache* tcache) {
    if (!tcache) return;
    
    atomic8_store(&tcache->active, 0);
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (tcache->bins[i].objects) {
            struct arena* arena = arena_get_current();
            for (size_t j = 0; j < tcache->bins[i].count; j++) {
                if (arena) {
                    arena_free(arena, tcache->bins[i].objects[j]);
                }
            }
            __libc_free(tcache->bins[i].objects);
        }
    }
    
    __libc_free(tcache);
}

void* tcache_alloc(struct tcache* tcache, size_t size) {
    if (!tcache || !atomic8_load(&tcache->active) || size == 0) {
        return NULL;
    }
    
    size_t class_idx = size_to_class(size);
    
    if (is_large_class_idx(class_idx)) {
        atomic64_inc(&tcache->cache_misses);
        return NULL;
    }
    
    struct tcache_bin* bin = &tcache->bins[class_idx];
    
    if (bin->count > 0) {
        void* ptr = bin->objects[--bin->count];
        
        atomic64_inc(&tcache->alloc_count);
        atomic64_inc(&tcache->cache_hits);
        
        prefetch_l1(ptr);
        
        log_trace("Tcache alloc: ptr=%p, size_class=%zu, hit_rate=%.2f",
                  ptr, class_idx, tcache_get_hit_rate(tcache));
        return ptr;
    }
    
    atomic64_inc(&tcache->alloc_count);
    atomic64_inc(&tcache->cache_misses);
    
    log_trace("Tcache alloc miss: size_class=%zu", class_idx);
    return NULL;
}

bool tcache_free(struct tcache* tcache, void* ptr) {
    if (!tcache || !ptr || !atomic8_load(&tcache->active)) {
        return false;
    }
    
    struct chunk* chunk = ptr_to_chunk(ptr);
    
    if (chunk->is_large) {
        return false;
    }
    
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t chunk_base = (uintptr_t)chunk->base_addr;
    size_t offset = addr - chunk_base;
    
    size_t class_idx = 0;
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        size_t block_size = class_to_size(i);
        if (block_size == 0) continue;
        if (offset % block_size == 0 && offset < chunk->size) {
            class_idx = i;
            break;
        }
    }
    
    struct tcache_bin* bin = &tcache->bins[class_idx];
    
    if (bin->count < bin->capacity) {
        bin->objects[bin->count++] = ptr;
        
        atomic64_inc(&tcache->free_count);
        
        return true;
    }
    
    return false;
}

void tcache_fill(struct tcache* tcache, size_t size) {
    if (!tcache || size == 0) return;
    
    size_t class_idx = size_to_class(size);
    
    if (is_large_class_idx(class_idx)) {
        return;
    }
    
    struct tcache_bin* bin = &tcache->bins[class_idx];
    
    struct arena* arena = arena_get_current();
    if (!arena) {
        log_warn("No arena available for tcache fill");
        return;
    }
    
    size_t fill_count = bin->capacity - bin->count;
    fill_count = min(fill_count, TCACHE_BIN_CAPACITY_SMALL / 2);
    
    for (size_t i = 0; i < fill_count; i++) {
        void* ptr = arena_malloc(arena, class_to_size(class_idx));
        if (ptr) {
            bin->objects[bin->count++] = ptr;
        } else {
            break;
        }
    }
    
    log_debug("Tcache fill: size_class=%zu, filled=%zu/%zu objects",
              class_idx, bin->count, bin->capacity);
}

void tcache_flush(struct tcache* tcache, size_t size) {
    if (!tcache || size == 0) return;
    
    size_t class_idx = size_to_class(size);
    
    if (is_large_class_idx(class_idx)) {
        return;
    }
    
    struct tcache_bin* bin = &tcache->bins[class_idx];
    
    struct arena* arena = arena_get_current();
    
    size_t flush_count = bin->count / 2;
    
    for (size_t i = 0; i < flush_count; i++) {
        if (bin->count > 0) {
            void* ptr = bin->objects[--bin->count];
            if (arena) {
                arena_free(arena, ptr);
            }
        }
    }
    
    log_debug("Tcache flush: size_class=%zu, flushed=%zu objects",
              class_idx, flush_count);
}

void tcache_adjust_capacity(struct tcache* tcache) {
    if (!tcache) return;
    
    double hit_rate = tcache_get_hit_rate(tcache);
    
    if (hit_rate > 0.9 && tcache->current_capacity < tcache->max_capacity) {
        size_t new_capacity = min(tcache->current_capacity * 1.2, 
                                  tcache->max_capacity);
        tcache->current_capacity = new_capacity;
        
        log_debug("Tcache capacity increased: %zu -> %zu (hit_rate=%.2f)",
                  tcache->current_capacity, new_capacity, hit_rate);
    } else if (hit_rate < 0.5 && tcache->current_capacity > NOMALLOC_TCACHE_SIZE_MIN) {
        size_t new_capacity = max(tcache->current_capacity * 0.8, 
                                  NOMALLOC_TCACHE_SIZE_MIN);
        tcache->current_capacity = new_capacity;
        
        log_debug("Tcache capacity decreased: %zu -> %zu (hit_rate=%.2f)",
                  tcache->current_capacity, new_capacity, hit_rate);
    }
}

void tcache_print_stats(struct tcache* tcache) {
    printf("Tcache Statistics:\n");
    printf("  Active: %s\n", atomic8_load(&tcache->active) ? "yes" : "no");
    printf("  Alloc count: %llu\n", atomic64_load(&tcache->alloc_count));
    printf("  Free count: %llu\n", atomic64_load(&tcache->free_count));
    printf("  Cache hits: %llu\n", atomic64_load(&tcache->cache_hits));
    printf("  Cache misses: %llu\n", atomic64_load(&tcache->cache_misses));
    printf("  Hit rate: %.2f%%\n", tcache_get_hit_rate(tcache) * 100);
    printf("  Current capacity: %zu bytes\n", tcache->current_capacity);
    printf("  Max capacity: %zu bytes\n", tcache->max_capacity);
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (tcache->bins[i].count > 0) {
            printf("  Size class %zu: %zu/%zu objects\n",
                   i, tcache->bins[i].count, tcache->bins[i].capacity);
        }
    }
}