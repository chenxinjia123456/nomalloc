#include "lirs_manager.h"
#include "../../core/size_class.h"
#include "../../memory/arena.h"
#include "../../utils/memory.h"
#include "../../utils/log.h"
#include "../../utils/math.h"
#include "../../utils/assert.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef NUM_SIZE_CLASSES
#define NUM_SIZE_CLASSES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#endif

#define LIRS_BIN_CAPACITY_SMALL    64
#define LIRS_BIN_CAPACITY_MEDIUM   32
#define LIRS_BIN_CAPACITY_LARGE    16

struct lirs_manager g_lirs_manager;

static void lirs_evict_callback(void* key, void* value, size_t size) {
    struct arena* arena = arena_get_current();
    if (arena && value) {
        arena_free(arena, value);
        log_trace("LIRS evict callback: ptr=%p, size=%zu", value, size);
    }
}

static size_t get_bin_capacity(size_t class_idx) {
    if (is_small_class_idx(class_idx)) {
        return LIRS_BIN_CAPACITY_SMALL;
    } else if (is_medium_class_idx(class_idx)) {
        return LIRS_BIN_CAPACITY_MEDIUM;
    } else {
        return LIRS_BIN_CAPACITY_LARGE;
    }
}

static size_t get_bin_max_size(size_t class_idx) {
    size_t capacity = get_bin_capacity(class_idx);
    size_t block_size = class_to_size(class_idx);
    return capacity * block_size;
}

int lirs_manager_init(void) {
    memset(&g_lirs_manager, 0, sizeof(g_lirs_manager));
    
    spinlock_init(&g_lirs_manager.global_lock);
    
    atomic64_init(&g_lirs_manager.hits, 0);
    atomic64_init(&g_lirs_manager.misses, 0);
    atomic64_init(&g_lirs_manager.evictions, 0);
    
    g_lirs_manager.total_capacity = 0;
    g_lirs_manager.total_max_size = NOMALLOC_TCACHE_SIZE_DEFAULT;
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (is_large_class_idx(i)) {
            g_lirs_manager.bins[i].cache = NULL;
            continue;
        }
        
        size_t capacity = get_bin_capacity(i);
        size_t max_size = get_bin_max_size(i);
        
        g_lirs_manager.bins[i].cache = lirs_create(capacity, max_size);
        if (!g_lirs_manager.bins[i].cache) {
            log_error("Failed to create LIRS cache for size class %zu", i);
            
            for (size_t j = 0; j < i; j++) {
                if (g_lirs_manager.bins[j].cache) {
                    lirs_destroy(g_lirs_manager.bins[j].cache);
                }
            }
            
            return -1;
        }
        
        lirs_set_evict_callback(g_lirs_manager.bins[i].cache, 
                               lirs_evict_callback, NULL);
        
        g_lirs_manager.bins[i].size_class = i;
        spinlock_init(&g_lirs_manager.bins[i].lock);
        
        g_lirs_manager.total_capacity += capacity;
    }
    
    log_info("LIRS manager initialized: total_capacity=%zu, total_max_size=%zu",
             g_lirs_manager.total_capacity, g_lirs_manager.total_max_size);
    
    return 0;
}

void lirs_manager_shutdown(void) {
    spinlock_lock(&g_lirs_manager.global_lock);
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (g_lirs_manager.bins[i].cache) {
            lirs_destroy(g_lirs_manager.bins[i].cache);
            g_lirs_manager.bins[i].cache = NULL;
        }
    }
    
    spinlock_unlock(&g_lirs_manager.global_lock);
    
    log_info("LIRS manager shutdown");
}

int lirs_manager_configure(size_t total_capacity, size_t max_size) {
    spinlock_lock(&g_lirs_manager.global_lock);
    
    g_lirs_manager.total_max_size = max_size;
    
    size_t per_class_max = max_size / NUM_SIZE_CLASSES;
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (g_lirs_manager.bins[i].cache) {
            size_t class_max = min(per_class_max, get_bin_max_size(i));
            lirs_resize(g_lirs_manager.bins[i].cache, get_bin_capacity(i));
        }
    }
    
    g_lirs_manager.total_capacity = total_capacity;
    
    spinlock_unlock(&g_lirs_manager.global_lock);
    
    log_info("LIRS manager configured: total_capacity=%zu, max_size=%zu",
             total_capacity, max_size);
    
    return 0;
}

void* lirs_manager_get(size_t size) {
    size_t class_idx = size_to_class(size);
    
    if (is_large_class_idx(class_idx)) {
        atomic64_inc(&g_lirs_manager.misses);
        return NULL;
    }
    
    struct lirs_bin* bin = &g_lirs_manager.bins[class_idx];
    
    if (!bin->cache) {
        atomic64_inc(&g_lirs_manager.misses);
        return NULL;
    }
    
    spinlock_lock(&bin->lock);
    
    void* ptr = lirs_get(bin->cache, (void*)size);
    
    if (ptr) {
        atomic64_inc(&g_lirs_manager.hits);
        log_trace("LIRS manager get: ptr=%p, size_class=%zu", ptr, class_idx);
    } else {
        atomic64_inc(&g_lirs_manager.misses);
        log_trace("LIRS manager miss: size_class=%zu", class_idx);
    }
    
    spinlock_unlock(&bin->lock);
    
    return ptr;
}

static void* allocate_from_arena(size_t class_idx) {
    struct arena* arena = arena_get_current();
    if (!arena) {
        return NULL;
    }
    
    size_t size = class_to_size(class_idx);
    return arena_malloc(arena, size);
}

int lirs_manager_put(void* ptr, size_t size) {
    if (!ptr) return -1;
    
    size_t class_idx = size_to_class(size);
    
    if (is_large_class_idx(class_idx)) {
        return -2;
    }
    
    struct lirs_bin* bin = &g_lirs_manager.bins[class_idx];
    
    if (!bin->cache) {
        return -3;
    }
    
    spinlock_lock(&bin->lock);
    
    int result = lirs_put(bin->cache, (void*)ptr, ptr, class_to_size(class_idx));
    
    spinlock_unlock(&bin->lock);
    
    if (result == 0) {
        log_trace("LIRS manager put: ptr=%p, size_class=%zu", ptr, class_idx);
    } else {
        log_trace("LIRS manager put failed: ptr=%p, size_class=%zu, result=%d",
                  ptr, class_idx, result);
    }
    
    return result;
}

int lirs_manager_remove(void* ptr) {
    if (!ptr) return -1;
    
    struct chunk* chunk = ptr_to_chunk(ptr);
    
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t chunk_base = (uintptr_t)chunk->base_addr;
    size_t offset = addr - chunk_base;
    
    size_t class_idx = 0;
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        size_t block_size = class_to_size(i);
        if (offset % block_size == 0 && offset < chunk->size) {
            class_idx = i;
            break;
        }
    }
    
    if (is_large_class_idx(class_idx)) {
        return -2;
    }
    
    struct lirs_bin* bin = &g_lirs_manager.bins[class_idx];
    
    if (!bin->cache) {
        return -3;
    }
    
    spinlock_lock(&bin->lock);
    
    int result = lirs_remove(bin->cache, ptr);
    
    spinlock_unlock(&bin->lock);
    
    log_trace("LIRS manager remove: ptr=%p, size_class=%zu", ptr, class_idx);
    
    return result;
}

void lirs_manager_print_stats(void) {
    printf("\nLIRS Manager Statistics:\n");
    printf("  Total capacity: %zu\n", g_lirs_manager.total_capacity);
    printf("  Total max size: %zu bytes\n", g_lirs_manager.total_max_size);
    printf("  Global hits: %llu\n", atomic64_load(&g_lirs_manager.hits));
    printf("  Global misses: %llu\n", atomic64_load(&g_lirs_manager.misses));
    printf("  Global hit rate: %.2f%%\n", lirs_manager_get_hit_rate() * 100);
    printf("  Global evictions: %llu\n", atomic64_load(&g_lirs_manager.evictions));
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (g_lirs_manager.bins[i].cache) {
            printf("\n  Size class %zu:\n", i);
            printf("    Hits: %llu\n", lirs_get_hits(g_lirs_manager.bins[i].cache));
            printf("    Misses: %llu\n", lirs_get_misses(g_lirs_manager.bins[i].cache));
            printf("    Hit rate: %.2f%%\n", 
                   lirs_get_hit_rate(g_lirs_manager.bins[i].cache) * 100);
        }
    }
}

int lirs_manager_get_global_stats(struct lirs_stats* stats) {
    if (!stats) return -1;
    
    stats->hits = g_lirs_manager.hits;
    stats->misses = g_lirs_manager.misses;
    stats->evictions = g_lirs_manager.evictions;
    stats->total_size = 0;
    stats->max_size = g_lirs_manager.total_max_size;
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (g_lirs_manager.bins[i].cache) {
            stats->total_size += atomic64_load(&g_lirs_manager.bins[i].cache->stats.total_size);
            stats->lir_count += atomic64_load(&g_lirs_manager.bins[i].cache->stats.lir_count);
            stats->hir_resident_count += atomic64_load(&g_lirs_manager.bins[i].cache->stats.hir_resident_count);
        }
    }
    
    return 0;
}