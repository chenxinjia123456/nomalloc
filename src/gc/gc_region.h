#ifndef NOMALLOC_GC_GC_REGION_H
#define NOMALLOC_GC_GC_REGION_H

#include "gc_types.h"
#include "../memory/arena.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gc_region_manager {
    struct gc_region* regions[GC_MAX_REGIONS];
    int num_regions;
    int max_regions;
    
    struct list_head free_regions;
    struct list_head young_regions;
    struct list_head survivor_regions;
    struct list_head old_regions;
    struct list_head humongous_regions;
    struct list_head pinned_regions;
    
    size_t free_region_count;
    size_t young_region_count;
    size_t survivor_region_count;
    size_t old_region_count;
    size_t humongous_region_count;
    size_t pinned_region_count;
    
    size_t total_size;
    size_t used_size;
    
    size_t young_gen_target_size;
    size_t old_gen_target_size;
    size_t survivor_target_size;
    
    spinlock_t lock;
};

extern struct gc_region_manager g_gc_region_manager;

int gc_region_manager_init(void);
void gc_region_manager_shutdown(void);

struct gc_region* gc_region_create(size_t size);
void gc_region_destroy(struct gc_region* region);

struct gc_region* gc_region_alloc(int type);
void gc_region_free(struct gc_region* region);

void gc_region_reclaim(struct gc_region* region);
void gc_region_compact(struct gc_region* region);

static inline size_t gc_region_get_size(struct gc_region* region) {
    return region->size;
}

static inline size_t gc_region_get_allocated(struct gc_region* region) {
    return region->allocated_bytes;
}

static inline size_t gc_region_get_live(struct gc_region* region) {
    return region->live_bytes;
}

static inline size_t gc_region_get_garbage(struct gc_region* region) {
    return region->garbage_bytes;
}

static inline double gc_region_get_utilization(struct gc_region* region) {
    size_t allocated = gc_region_get_allocated(region);
    size_t size = gc_region_get_size(region);
    if (size == 0) return 0.0;
    return (double)allocated / (double)size;
}

static inline double gc_region_get_live_ratio(struct gc_region* region) {
    size_t allocated = gc_region_get_allocated(region);
    size_t live = gc_region_get_live(region);
    if (allocated == 0) return 0.0;
    return (double)live / (double)allocated;
}

static inline bool gc_region_is_empty(struct gc_region* region) {
    return gc_region_get_allocated(region) == 0;
}

static inline bool gc_region_is_full(struct gc_region* region) {
    return gc_region_get_allocated(region) >= gc_region_get_size(region);
}

static inline bool gc_region_needs_reclaim(struct gc_region* region) {
    return gc_region_get_live_ratio(region) < 0.5;
}

void gc_region_mark_object(struct gc_region* region, void* ptr);
void gc_region_unmark_object(struct gc_region* region, void* ptr);
bool gc_region_is_marked(struct gc_region* region, void* ptr);

void gc_region_set_live(struct gc_region* region, void* ptr);
bool gc_region_is_live(struct gc_region* region, void* ptr);

void gc_region_promote(struct gc_region* region);
void gc_region_age(struct gc_region* region);

void gc_region_manager_print_stats(void);
void gc_region_print_stats(struct gc_region* region);

static inline struct gc_region* gc_region_get_by_addr(void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    for (int i = 0; i < g_gc_region_manager.num_regions; i++) {
        struct gc_region* region = g_gc_region_manager.regions[i];
        if (region && addr >= (uintptr_t)region->base_addr &&
            addr < (uintptr_t)region->base_addr + region->size) {
            return region;
        }
    }
    return NULL;
}

static inline int gc_region_manager_get_available_regions(void) {
    return g_gc_region_manager.max_regions - g_gc_region_manager.num_regions +
           g_gc_region_manager.free_region_count;
}

static inline size_t gc_region_manager_get_available_size(void) {
    return gc_region_manager_get_available_regions() * 
           g_gc_region_manager.regions[0]->size;
}

#ifdef __cplusplus
}
#endif

#endif