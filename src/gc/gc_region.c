#include "gc_region.h"
#include "gc_types.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include "../memory/arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct gc_region_manager g_gc_region_manager;

static size_t g_region_size = GC_REGION_SIZE_DEFAULT;

int gc_region_manager_init(void) {
    memset(&g_gc_region_manager, 0, sizeof(g_gc_region_manager));
    
    g_gc_region_manager.max_regions = GC_MAX_REGIONS;
    g_region_size = GC_REGION_SIZE_DEFAULT;
    
    INIT_LIST_HEAD(&g_gc_region_manager.free_regions);
    INIT_LIST_HEAD(&g_gc_region_manager.young_regions);
    INIT_LIST_HEAD(&g_gc_region_manager.survivor_regions);
    INIT_LIST_HEAD(&g_gc_region_manager.old_regions);
    INIT_LIST_HEAD(&g_gc_region_manager.humongous_regions);
    INIT_LIST_HEAD(&g_gc_region_manager.pinned_regions);
    
    spinlock_init(&g_gc_region_manager.lock);
    
    g_gc_region_manager.young_gen_target_size = g_region_size * 10;
    g_gc_region_manager.survivor_target_size = g_region_size * 2;
    g_gc_region_manager.old_gen_target_size = g_region_size * 100;
    
    log_info("GC region manager initialized: region_size=%zu, max_regions=%d",
             g_region_size, g_gc_region_manager.max_regions);
    
    return 0;
}

void gc_region_manager_shutdown(void) {
    spinlock_lock(&g_gc_region_manager.lock);
    
    for (int i = 0; i < g_gc_region_manager.num_regions; i++) {
        if (g_gc_region_manager.regions[i]) {
            gc_region_destroy(g_gc_region_manager.regions[i]);
            g_gc_region_manager.regions[i] = NULL;
        }
    }
    
    g_gc_region_manager.num_regions = 0;
    
    spinlock_unlock(&g_gc_region_manager.lock);
    
    log_info("GC region manager shutdown");
}

struct gc_region* gc_region_create(size_t size) {
    struct gc_region* region = (struct gc_region*)malloc(sizeof(struct gc_region));
    if (!region) {
        log_error("Failed to allocate GC region metadata");
        return NULL;
    }
    
    memset(region, 0, sizeof(struct gc_region));
    
    region->size = align_up(size, g_region_size);
    region->base_addr = memory_alloc_aligned(region->size, g_region_size);
    
    if (!region->base_addr) {
        free(region);
        log_error("Failed to allocate GC region memory: %zu bytes", region->size);
        return NULL;
    }
    
    region->type = GC_REGION_EMPTY;
    region->prev_type = GC_REGION_EMPTY;
    
    region->bitmap_size = region->size / 8;
    
    size_t bitmap_alloc_sz = bitmap_alloc_size(region->bitmap_size);
    region->marking_bitmap = (bitmap_t)malloc(bitmap_alloc_sz);
    region->live_bitmap = (bitmap_t)malloc(bitmap_alloc_sz);
    
    if (!region->marking_bitmap || !region->live_bitmap) {
        if (region->marking_bitmap) free(region->marking_bitmap);
        if (region->live_bitmap) free(region->live_bitmap);
        memory_free_aligned(region->base_addr, region->size, g_region_size);
        free(region);
        log_error("Failed to allocate GC region bitmaps");
        return NULL;
    }
    
    bitmap_init(region->marking_bitmap, region->bitmap_size, false);
    bitmap_init(region->live_bitmap, region->bitmap_size, false);
    
    spinlock_init(&region->lock);
    INIT_LIST_HEAD(&region->list);
    INIT_LIST_HEAD(&region->reclaim_list);
    INIT_LIST_HEAD(&region->compact_list);
    
    log_debug("Created GC region: id=%d, base=%p, size=%zu", 
              region->id, region->base_addr, region->size);
    
    return region;
}

void gc_region_destroy(struct gc_region* region) {
    if (!region) return;
    
    spinlock_lock(&region->lock);
    
    if (region->marking_bitmap) free(region->marking_bitmap);
    if (region->live_bitmap) free(region->live_bitmap);
    
    memory_free_aligned(region->base_addr, region->size, g_region_size);
    
    spinlock_unlock(&region->lock);
    
    free(region);
    
    log_debug("Destroyed GC region: id=%d", region->id);
}

struct gc_region* gc_region_alloc(int type) {
    spinlock_lock(&g_gc_region_manager.lock);
    
    struct gc_region* region = NULL;
    
    if (!list_empty(&g_gc_region_manager.free_regions)) {
        region = list_first_entry(&g_gc_region_manager.free_regions, 
                                  struct gc_region, list);
        list_del(&region->list);
        g_gc_region_manager.free_region_count--;
    } else if (g_gc_region_manager.num_regions < g_gc_region_manager.max_regions) {
        region = gc_region_create(g_region_size);
        if (!region) {
            spinlock_unlock(&g_gc_region_manager.lock);
            return NULL;
        }
        
        region->id = g_gc_region_manager.num_regions;
        g_gc_region_manager.regions[g_gc_region_manager.num_regions++] = region;
        g_gc_region_manager.total_size += region->size;
    } else {
        spinlock_unlock(&g_gc_region_manager.lock);
        log_error("No available GC regions");
        return NULL;
    }
    
    region->type = type;
    region->prev_type = GC_REGION_EMPTY;
    region->age = 0;
    region->gc_epoch = 0;
    region->allocated_bytes = 0;
    region->live_bytes = 0;
    region->garbage_bytes = 0;
    region->needs_reclaim = false;
    region->is_compacting_target = false;
    region->is_relocation_target = false;
    
    switch (type) {
        case GC_REGION_YOUNG:
            list_add_tail(&region->list, &g_gc_region_manager.young_regions);
            g_gc_region_manager.young_region_count++;
            g_gc_region_manager.used_size += region->size;
            break;
        case GC_REGION_SURVIVOR:
            list_add_tail(&region->list, &g_gc_region_manager.survivor_regions);
            g_gc_region_manager.survivor_region_count++;
            g_gc_region_manager.used_size += region->size;
            break;
        case GC_REGION_OLD:
            list_add_tail(&region->list, &g_gc_region_manager.old_regions);
            g_gc_region_manager.old_region_count++;
            g_gc_region_manager.used_size += region->size;
            break;
        case GC_REGION_HUMONGOUS:
            list_add_tail(&region->list, &g_gc_region_manager.humongous_regions);
            g_gc_region_manager.humongous_region_count++;
            g_gc_region_manager.used_size += region->size;
            break;
        case GC_REGION_PINNED:
            list_add_tail(&region->list, &g_gc_region_manager.pinned_regions);
            g_gc_region_manager.pinned_region_count++;
            g_gc_region_manager.used_size += region->size;
            break;
        default:
            list_add_tail(&region->list, &g_gc_region_manager.free_regions);
            g_gc_region_manager.free_region_count++;
            break;
    }
    
    spinlock_unlock(&g_gc_region_manager.lock);
    
    log_trace("Allocated GC region: id=%d, type=%d", region->id, type);
    
    return region;
}

void gc_region_free(struct gc_region* region) {
    if (!region) return;
    
    spinlock_lock(&g_gc_region_manager.lock);
    
    list_del(&region->list);
    
    switch (region->type) {
        case GC_REGION_YOUNG:
            g_gc_region_manager.young_region_count--;
            break;
        case GC_REGION_SURVIVOR:
            g_gc_region_manager.survivor_region_count--;
            break;
        case GC_REGION_OLD:
            g_gc_region_manager.old_region_count--;
            break;
        case GC_REGION_HUMONGOUS:
            g_gc_region_manager.humongous_region_count--;
            break;
        case GC_REGION_PINNED:
            g_gc_region_manager.pinned_region_count--;
            break;
    }
    
    g_gc_region_manager.used_size -= region->size;
    
    region->type = GC_REGION_EMPTY;
    region->prev_type = GC_REGION_EMPTY;
    region->age = 0;
    region->allocated_bytes = 0;
    region->live_bytes = 0;
    region->garbage_bytes = 0;
    
    memory_zero(region->base_addr, region->size);
    
    list_add_tail(&region->list, &g_gc_region_manager.free_regions);
    g_gc_region_manager.free_region_count++;
    
    spinlock_unlock(&g_gc_region_manager.lock);
    
    log_trace("Freed GC region: id=%d", region->id);
}

void gc_region_reclaim(struct gc_region* region) {
    if (!region) return;
    
    spinlock_lock(&region->lock);
    
    region->garbage_bytes = region->allocated_bytes - region->live_bytes;
    
    if (region->garbage_bytes > region->allocated_bytes * 0.5) {
        region->needs_reclaim = true;
    }
    
    memory_zero(region->base_addr + region->live_bytes, 
                region->allocated_bytes - region->live_bytes);
    
    region->allocated_bytes = region->live_bytes;
    
    spinlock_unlock(&region->lock);
    
    log_trace("Reclaimed GC region: id=%d, garbage=%zu bytes, live=%zu bytes",
              region->id, region->garbage_bytes, region->live_bytes);
}

void gc_region_compact(struct gc_region* region) {
    if (!region || !region->is_compacting_target) return;
    
    spinlock_lock(&region->lock);
    
    log_debug("Compacting GC region: id=%d, allocated=%zu, live=%zu",
              region->id, region->allocated_bytes, region->live_bytes);
    
    region->allocated_bytes = region->live_bytes;
    region->is_compacting_target = false;
    
    spinlock_unlock(&region->lock);
}

void gc_region_mark_object(struct gc_region* region, void* ptr) {
    if (!region || !ptr) return;
    
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)region->base_addr;
    size_t bit_idx = offset / 8;
    
    bitmap_set(region->marking_bitmap, bit_idx);
}

void gc_region_unmark_object(struct gc_region* region, void* ptr) {
    if (!region || !ptr) return;
    
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)region->base_addr;
    size_t bit_idx = offset / 8;
    
    bitmap_clear_bit(region->marking_bitmap, bit_idx);
}

bool gc_region_is_marked(struct gc_region* region, void* ptr) {
    if (!region || !ptr) return false;
    
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)region->base_addr;
    size_t bit_idx = offset / 8;
    
    return bitmap_get(region->marking_bitmap, bit_idx);
}

void gc_region_set_live(struct gc_region* region, void* ptr) {
    if (!region || !ptr) return;
    
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)region->base_addr;
    size_t bit_idx = offset / 8;
    
    bitmap_set(region->live_bitmap, bit_idx);
}

bool gc_region_is_live(struct gc_region* region, void* ptr) {
    if (!region || !ptr) return false;
    
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)region->base_addr;
    size_t bit_idx = offset / 8;
    
    return bitmap_get(region->live_bitmap, bit_idx);
}

void gc_region_promote(struct gc_region* region) {
    if (!region) return;
    
    spinlock_lock(&g_gc_region_manager.lock);
    
    list_del(&region->list);
    
    switch (region->type) {
        case GC_REGION_YOUNG:
            g_gc_region_manager.young_region_count--;
            region->type = GC_REGION_SURVIVOR;
            list_add_tail(&region->list, &g_gc_region_manager.survivor_regions);
            g_gc_region_manager.survivor_region_count++;
            break;
        case GC_REGION_SURVIVOR:
            g_gc_region_manager.survivor_region_count--;
            region->type = GC_REGION_OLD;
            list_add_tail(&region->list, &g_gc_region_manager.old_regions);
            g_gc_region_manager.old_region_count++;
            break;
        default:
            break;
    }
    
    spinlock_unlock(&g_gc_region_manager.lock);
    
    log_trace("Promoted GC region: id=%d, type=%d", region->id, region->type);
}

void gc_region_age(struct gc_region* region) {
    if (!region) return;
    
    spinlock_lock(&region->lock);
    region->age++;
    
    if (region->type == GC_REGION_YOUNG && region->age > 1) {
        gc_region_promote(region);
    } else if (region->type == GC_REGION_SURVIVOR && region->age > 15) {
        gc_region_promote(region);
    }
    
    spinlock_unlock(&region->lock);
}

void gc_region_manager_print_stats(void) {
    printf("\nGC Region Manager Statistics:\n");
    printf("  Total regions: %d\n", g_gc_region_manager.num_regions);
    printf("  Max regions: %d\n", g_gc_region_manager.max_regions);
    printf("  Free regions: %zu\n", g_gc_region_manager.free_region_count);
    printf("  Young regions: %zu\n", g_gc_region_manager.young_region_count);
    printf("  Survivor regions: %zu\n", g_gc_region_manager.survivor_region_count);
    printf("  Old regions: %zu\n", g_gc_region_manager.old_region_count);
    printf("  Humongous regions: %zu\n", g_gc_region_manager.humongous_region_count);
    printf("  Pinned regions: %zu\n", g_gc_region_manager.pinned_region_count);
    printf("  Total size: %zu bytes\n", g_gc_region_manager.total_size);
    printf("  Used size: %zu bytes\n", g_gc_region_manager.used_size);
    printf("  Available size: %zu bytes\n", gc_region_manager_get_available_size());
    printf("  Young gen target: %zu bytes\n", g_gc_region_manager.young_gen_target_size);
    printf("  Survivor target: %zu bytes\n", g_gc_region_manager.survivor_target_size);
    printf("  Old gen target: %zu bytes\n", g_gc_region_manager.old_gen_target_size);
}

void gc_region_print_stats(struct gc_region* region) {
    if (!region) return;
    
    printf("GC Region %d Statistics:\n", region->id);
    printf("  Type: %d\n", region->type);
    printf("  Base address: %p\n", region->base_addr);
    printf("  Size: %zu bytes\n", region->size);
    printf("  Allocated: %zu bytes\n", gc_region_get_allocated(region));
    printf("  Live: %zu bytes\n", gc_region_get_live(region));
    printf("  Garbage: %zu bytes\n", gc_region_get_garbage(region));
    printf("  Utilization: %.2f%%\n", gc_region_get_utilization(region) * 100);
    printf("  Live ratio: %.2f%%\n", gc_region_get_live_ratio(region) * 100);
    printf("  Age: %u\n", region->age);
    printf("  GC epoch: %u\n", region->gc_epoch);
    printf("  Needs reclaim: %s\n", region->needs_reclaim ? "yes" : "no");
}