#include "gc.h"
#include "gc_region.h"
#include "gc_types.h"
#include <nomalloc/gc_api.h>
#include "../memory/arena.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include "../utils/time.h"
#include "../core/allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

struct gc g_gc;

static void gc_marking_context_init(struct gc_marking_context* ctx) {
    ctx->gray_regions = (struct gc_region**)malloc(GC_MAX_REGIONS * sizeof(struct gc_region*));
    ctx->gray_capacity = GC_MAX_REGIONS;
    ctx->gray_count = 0;
    
    ctx->mark_stack = (void**)malloc(1024 * sizeof(void*));
    ctx->mark_stack_capacity = 1024;
    ctx->mark_stack_top = 0;
    
    atomic8_init(&ctx->marking_active, 0);
    atomic8_init(&ctx->marking_complete, 0);
    
    ctx->marked_objects = 0;
    ctx->marked_bytes = 0;
}

static void gc_marking_context_destroy(struct gc_marking_context* ctx) {
    free(ctx->gray_regions);
    free(ctx->mark_stack);
}

static void gc_satb_buffer_init(struct gc_satb_buffer* buf, size_t capacity) {
    buf->buffer = (void**)malloc(capacity * sizeof(void*));
    buf->capacity = capacity;
    buf->count = 0;
    
    atomic8_init(&buf->active, 0);
    spinlock_init(&buf->lock);
}

static void gc_satb_buffer_destroy(struct gc_satb_buffer* buf) {
    free(buf->buffer);
}

int gc_init(void) {
    if (g_gc.initialized) {
        log_warn("GC already initialized");
        return 0;
    }
    
    memset(&g_gc, 0, sizeof(g_gc));
    
    g_gc.config.region_size = GC_REGION_SIZE_DEFAULT;
    g_gc.config.max_regions = GC_MAX_REGIONS;
    g_gc.config.young_gen_ratio = GC_YOUNG_GEN_RATIO;
    g_gc.config.survivor_ratio = GC_SURVIVOR_RATIO;
    g_gc.config.old_gen_ratio = GC_OLD_GEN_RATIO;
    g_gc.config.pause_target_ms = GC_PAUSE_TARGET_MS;
    g_gc.config.pause_target_min_ms = GC_PAUSE_TARGET_MIN_MS;
    g_gc.config.pause_target_max_ms = GC_PAUSE_TARGET_MAX_MS;
    g_gc.config.concurrent_marking = true;
    g_gc.config.parallel_reclaim = true;
    g_gc.config.mixed_gc_enabled = true;
    g_gc.config.marking_threads = 2;
    g_gc.config.reclaim_threads = 4;
    g_gc.config.gc_watermark_high = 0.8;
    g_gc.config.gc_watermark_low = 0.6;
    g_gc.config.gc_threshold = 100 * 1024 * 1024;
    
    g_gc.threshold_bytes = g_gc.config.gc_threshold;
    g_gc.watermark_high = g_gc.config.gc_watermark_high;
    g_gc.watermark_low = g_gc.config.gc_watermark_low;
    g_gc.pause_target_ms = g_gc.config.pause_target_ms;
    
    g_gc.young_gen_capacity = (size_t)(g_gc.config.region_size * GC_MAX_REGIONS * g_gc.config.young_gen_ratio);
    g_gc.survivor_capacity = (size_t)(g_gc.config.region_size * GC_MAX_REGIONS * g_gc.config.survivor_ratio);
    g_gc.old_gen_capacity = (size_t)(g_gc.config.region_size * GC_MAX_REGIONS * g_gc.config.old_gen_ratio);
    
    g_gc.current_epoch = 0;
    g_gc.state = GC_STATE_IDLE;
    g_gc.phase = GC_PHASE_IDLE;
    
    atomic8_init(&g_gc.active, 1);
    atomic8_init(&g_gc.concurrent_active, 0);
    
    atomic64_init(&g_gc.stats.gc_count, 0);
    atomic64_init(&g_gc.stats.young_gc_count, 0);
    atomic64_init(&g_gc.stats.mixed_gc_count, 0);
    atomic64_init(&g_gc.stats.full_gc_count, 0);
    atomic64_init(&g_gc.stats.gc_time_ns, 0);
    atomic64_init(&g_gc.stats.pause_time_ns, 0);
    atomic64_init(&g_gc.stats.bytes_freed, 0);
    atomic64_init(&g_gc.stats.regions_reclaimed, 0);
    atomic64_init(&g_gc.stats.young_gen_size, 0);
    atomic64_init(&g_gc.stats.old_gen_size, 0);
    atomic64_init(&g_gc.stats.survivor_size, 0);
    atomic64_init(&g_gc.stats.fragmentation_bytes, 0);
    atomic64_init(&g_gc.stats.compaction_count, 0);
    atomic64_init(&g_gc.stats.mark_time_ns, 0);
    atomic64_init(&g_gc.stats.reclaim_time_ns, 0);
    atomic64_init(&g_gc.stats.compact_time_ns, 0);
    
    mutex_init(&g_gc.gc_lock);
    spinlock_init(&g_gc.marking_lock);
    cond_init(&g_gc.gc_cond);
    
    g_gc.region_manager = &g_gc_region_manager;
    
    if (gc_region_manager_init() != 0) {
        log_error("Failed to initialize GC region manager");
        return -1;
    }
    
    gc_marking_context_init(&g_gc.marking_ctx);
    
    g_gc.num_satb_buffers = NOMALLOC_MAX_THREADS;
    g_gc.satb_buffers = (struct gc_satb_buffer**)malloc(g_gc.num_satb_buffers * sizeof(struct gc_satb_buffer*));
    for (size_t i = 0; i < g_gc.num_satb_buffers; i++) {
        g_gc.satb_buffers[i] = (struct gc_satb_buffer*)malloc(sizeof(struct gc_satb_buffer));
        gc_satb_buffer_init(g_gc.satb_buffers[i], 256);
    }
    
    g_gc.initialized = true;
    
    log_info("GC initialized: region_size=%zu, threshold=%zu bytes, pause_target=%zu ms",
             g_gc.config.region_size, g_gc.threshold_bytes, g_gc.pause_target_ms);
    
    return 0;
}

void gc_shutdown(void) {
    if (!g_gc.initialized) {
        log_warn("GC not initialized");
        return;
    }
    
    atomic8_store(&g_gc.active, 0);
    atomic8_store(&g_gc.concurrent_active, 0);
    
    mutex_lock(&g_gc.gc_lock);
    
    gc_region_manager_shutdown();
    
    gc_marking_context_destroy(&g_gc.marking_ctx);
    
    for (size_t i = 0; i < g_gc.num_satb_buffers; i++) {
        gc_satb_buffer_destroy(g_gc.satb_buffers[i]);
        free(g_gc.satb_buffers[i]);
    }
    free(g_gc.satb_buffers);
    
    g_gc.initialized = false;
    
    mutex_unlock(&g_gc.gc_lock);
    
    mutex_destroy(&g_gc.gc_lock);
    cond_destroy(&g_gc.gc_cond);
    
    log_info("GC shutdown: gc_count=%llu, bytes_freed=%llu",
             gc_get_count(), gc_get_bytes_freed());
}

static void gc_mark_region(struct gc_region* region) {
    if (!region || region->type == GC_REGION_EMPTY) return;
    
    spinlock_lock(&region->lock);
    
    region->live_bytes = 0;
    
    bitmap_init(region->live_bitmap, region->bitmap_size, false);
    bitmap_init(region->marking_bitmap, region->bitmap_size, false);
    
    region->live_bytes = region->allocated_bytes;
    
    spinlock_unlock(&region->lock);
    
    g_gc.marking_ctx.marked_bytes += region->live_bytes;
}

static void gc_mark_all_regions(void) {
    struct gc_region* region;
    
    list_for_each_entry(region, &g_gc_region_manager.young_regions, list) {
        gc_mark_region(region);
    }
    
    list_for_each_entry(region, &g_gc_region_manager.survivor_regions, list) {
        gc_mark_region(region);
    }
    
    list_for_each_entry(region, &g_gc_region_manager.old_regions, list) {
        gc_mark_region(region);
    }
}

static size_t gc_reclaim_regions(int max_regions, uint64_t* pause_time) {
    size_t bytes_freed = 0;
    int reclaimed = 0;
    
    uint64_t start_time = get_time_ns();
    
    struct gc_region* region;
    struct gc_region* next;
    
    list_for_each_entry_safe(region, next, &g_gc_region_manager.old_regions, reclaim_list) {
        if (reclaimed >= max_regions) break;
        
        if (gc_region_needs_reclaim(region) && !region->is_compacting_target) {
            gc_region_reclaim(region);
            bytes_freed += region->garbage_bytes;
            reclaimed++;
            
            atomic64_inc(&g_gc.stats.regions_reclaimed);
            
            if (gc_region_is_empty(region)) {
                gc_region_free(region);
            }
        }
    }
    
    uint64_t end_time = get_time_ns();
    *pause_time = end_time - start_time;
    
    return bytes_freed;
}

int gc_collect(void) {
    if (!g_gc.initialized || !atomic8_load(&g_gc.active)) {
        return -1;
    }
    
    mutex_lock(&g_gc.gc_lock);
    
    uint64_t start_time = get_time_ns();
    
    g_gc.current_epoch++;
    g_gc.state = GC_STATE_YOUNG_GC;
    g_gc.phase = GC_PHASE_MARK_START;
    
    atomic8_store(&g_gc.concurrent_active, 1);
    
    atomic64_inc(&g_gc.stats.gc_count);
    atomic64_inc(&g_gc.stats.young_gc_count);
    
    gc_mark_all_regions();
    
    g_gc.phase = GC_PHASE_CLEANUP;
    
    uint64_t pause_time = 0;
    size_t bytes_freed = gc_reclaim_regions(10, &pause_time);
    
    atomic64_add_fetch(&g_gc.stats.bytes_freed, bytes_freed);
    atomic64_add_fetch(&g_gc.stats.pause_time_ns, pause_time);
    
    g_gc.state = GC_STATE_IDLE;
    g_gc.phase = GC_PHASE_IDLE;
    
    atomic8_store(&g_gc.concurrent_active, 0);
    
    uint64_t end_time = get_time_ns();
    atomic64_add_fetch(&g_gc.stats.gc_time_ns, end_time - start_time);
    
    mutex_unlock(&g_gc.gc_lock);
    
    log_info("GC collection completed: epoch=%llu, bytes_freed=%zu, pause_time=%.2f ms",
             g_gc.current_epoch, bytes_freed, pause_time / 1000000.0);
    
    return 0;
}

int gc_collect_async(void) {
    if (!g_gc.initialized || !atomic8_load(&g_gc.active)) {
        return -1;
    }
    
    if (atomic8_load(&g_gc.concurrent_active)) {
        return -2;
    }
    
    atomic8_store(&g_gc.concurrent_active, 1);
    
    return 0;
}

int gc_collect_wait(void) {
    while (atomic8_load(&g_gc.concurrent_active)) {
        cond_wait(&g_gc.gc_cond, &g_gc.gc_lock);
    }
    
    return 0;
}

int gc_collect_young(void) {
    if (!g_gc.initialized || !atomic8_load(&g_gc.active)) {
        return -1;
    }
    
    mutex_lock(&g_gc.gc_lock);
    
    g_gc.state = GC_STATE_YOUNG_GC;
    g_gc.current_epoch++;
    
    atomic64_inc(&g_gc.stats.gc_count);
    atomic64_inc(&g_gc.stats.young_gc_count);
    
    struct gc_region* region;
    list_for_each_entry(region, &g_gc_region_manager.young_regions, list) {
        gc_mark_region(region);
        gc_region_age(region);
    }
    
    uint64_t pause_time = 0;
    size_t bytes_freed = gc_reclaim_regions(5, &pause_time);
    
    atomic64_add_fetch(&g_gc.stats.bytes_freed, bytes_freed);
    atomic64_add_fetch(&g_gc.stats.pause_time_ns, pause_time);
    
    g_gc.state = GC_STATE_IDLE;
    
    mutex_unlock(&g_gc.gc_lock);
    
    log_debug("Young GC completed: bytes_freed=%zu", bytes_freed);
    
    return 0;
}

int gc_collect_mixed(void) {
    if (!g_gc.initialized || !atomic8_load(&g_gc.active)) {
        return -1;
    }
    
    if (!g_gc.config.mixed_gc_enabled) {
        return gc_collect_young();
    }
    
    mutex_lock(&g_gc.gc_lock);
    
    g_gc.state = GC_STATE_MIXED_GC;
    g_gc.phase = GC_PHASE_MIXED_RECLAIM_START;
    g_gc.current_epoch++;
    
    atomic64_inc(&g_gc.stats.gc_count);
    atomic64_inc(&g_gc.stats.mixed_gc_count);
    
    struct gc_region* region;
    list_for_each_entry(region, &g_gc_region_manager.young_regions, list) {
        gc_mark_region(region);
        gc_region_age(region);
    }
    
    g_gc.phase = GC_PHASE_MIXED_RECLAIM;
    
    uint64_t pause_time = 0;
    size_t bytes_freed = gc_reclaim_regions(15, &pause_time);
    
    atomic64_add_fetch(&g_gc.stats.bytes_freed, bytes_freed);
    atomic64_add_fetch(&g_gc.stats.pause_time_ns, pause_time);
    
    g_gc.state = GC_STATE_IDLE;
    g_gc.phase = GC_PHASE_IDLE;
    
    mutex_unlock(&g_gc.gc_lock);
    
    log_debug("Mixed GC completed: bytes_freed=%zu", bytes_freed);
    
    return 0;
}

int gc_collect_full(void) {
    if (!g_gc.initialized || !atomic8_load(&g_gc.active)) {
        return -1;
    }
    
    mutex_lock(&g_gc.gc_lock);
    
    g_gc.state = GC_STATE_FULL_GC;
    g_gc.current_epoch++;
    
    atomic64_inc(&g_gc.stats.gc_count);
    atomic64_inc(&g_gc.stats.full_gc_count);
    
    gc_mark_all_regions();
    
    struct gc_region* region;
    list_for_each_entry(region, &g_gc_region_manager.young_regions, list) {
        gc_region_age(region);
    }
    
    uint64_t pause_time = 0;
    size_t bytes_freed = gc_reclaim_regions(50, &pause_time);
    
    atomic64_add_fetch(&g_gc.stats.bytes_freed, bytes_freed);
    atomic64_add_fetch(&g_gc.stats.pause_time_ns, pause_time);
    
    g_gc.state = GC_STATE_IDLE;
    
    mutex_unlock(&g_gc.gc_lock);
    
    log_info("Full GC completed: bytes_freed=%zu", bytes_freed);
    
    return 0;
}

int gc_enable(void) {
    if (!g_gc.initialized) return -1;
    atomic8_store(&g_gc.active, 1);
    log_debug("GC enabled");
    return 0;
}

int gc_disable(void) {
    if (!g_gc.initialized) return -1;
    atomic8_store(&g_gc.active, 0);
    log_debug("GC disabled");
    return 0;
}

bool gc_is_enabled(void) {
    return atomic8_load(&g_gc.active);
}

int gc_set_threshold(size_t threshold) {
    if (threshold == 0) return -1;
    g_gc.threshold_bytes = threshold;
    g_gc.config.gc_threshold = threshold;
    log_debug("GC threshold set: %zu bytes", threshold);
    return 0;
}

size_t gc_get_threshold(void) {
    return g_gc.threshold_bytes;
}

int gc_set_watermark(double high, double low) {
    if (high <= low || high > 1.0 || low < 0.0) return -1;
    g_gc.watermark_high = high;
    g_gc.watermark_low = low;
    g_gc.config.gc_watermark_high = high;
    g_gc.config.gc_watermark_low = low;
    log_debug("GC watermark set: high=%.2f, low=%.2f", high, low);
    return 0;
}

void gc_get_watermark(double* high, double* low) {
    *high = g_gc.watermark_high;
    *low = g_gc.watermark_low;
}

int gc_set_region_size(size_t size) {
    if (size < GC_REGION_SIZE_MIN || size > GC_REGION_SIZE_MAX) return -1;
    g_gc.config.region_size = size;
    log_debug("GC region size set: %zu bytes", size);
    return 0;
}

size_t gc_get_region_size(void) {
    return g_gc.config.region_size;
}

int gc_set_pause_target(uint64_t target_ms) {
    if (target_ms < GC_PAUSE_TARGET_MIN_MS || target_ms > GC_PAUSE_TARGET_MAX_MS) return -1;
    g_gc.pause_target_ms = target_ms;
    g_gc.config.pause_target_ms = target_ms;
    log_debug("GC pause target set: %llu ms", target_ms);
    return 0;
}

uint64_t gc_get_pause_target(void) {
    return g_gc.pause_target_ms;
}

int gc_set_generation_threshold(uint8_t generation, size_t threshold) {
    if (generation > 2 || threshold == 0) return -1;
    
    switch (generation) {
        case 0:
            g_gc.young_gen_capacity = threshold;
            break;
        case 1:
            g_gc.survivor_capacity = threshold;
            break;
        case 2:
            g_gc.old_gen_capacity = threshold;
            break;
    }
    
    log_debug("GC generation %u threshold set: %zu bytes", generation, threshold);
    return 0;
}

int gc_set_mixed_gc_ratio(double ratio) {
    if (ratio < 0.0 || ratio > 1.0) return -1;
    g_gc.config.mixed_gc_enabled = ratio > 0.0;
    log_debug("GC mixed GC ratio set: %.2f", ratio);
    return 0;
}

int gc_get_stats(struct gc_stats* stats) {
    if (!stats) return -1;
    
    stats->gc_count = g_gc.stats.gc_count;
    stats->young_gc_count = g_gc.stats.young_gc_count;
    stats->mixed_gc_count = g_gc.stats.mixed_gc_count;
    stats->full_gc_count = g_gc.stats.full_gc_count;
    stats->gc_time_ns = g_gc.stats.gc_time_ns;
    stats->pause_time_ns = g_gc.stats.pause_time_ns;
    stats->bytes_freed = g_gc.stats.bytes_freed;
    stats->regions_reclaimed = g_gc.stats.regions_reclaimed;
    stats->young_gen_size = g_gc.stats.young_gen_size;
    stats->old_gen_size = g_gc.stats.old_gen_size;
    stats->survivor_size = g_gc.stats.survivor_size;
    
    return 0;
}

int gc_reset_stats(void) {
    atomic64_store(&g_gc.stats.gc_count, 0);
    atomic64_store(&g_gc.stats.young_gc_count, 0);
    atomic64_store(&g_gc.stats.mixed_gc_count, 0);
    atomic64_store(&g_gc.stats.full_gc_count, 0);
    atomic64_store(&g_gc.stats.gc_time_ns, 0);
    atomic64_store(&g_gc.stats.pause_time_ns, 0);
    atomic64_store(&g_gc.stats.bytes_freed, 0);
    atomic64_store(&g_gc.stats.regions_reclaimed, 0);
    
    log_debug("GC stats reset");
    return 0;
}

int gc_print_stats(void) {
    printf("\nGarbage Collection Statistics:\n");
    printf("  GC count: %llu\n", gc_get_count());
    printf("  Young GC count: %llu\n", gc_get_young_gc_count());
    printf("  Mixed GC count: %llu\n", gc_get_mixed_gc_count());
    printf("  Full GC count: %llu\n", gc_get_full_gc_count());
    printf("  Total GC time: %.2f ms\n", gc_get_total_gc_time() / 1000000.0);
    printf("  Avg GC time: %.2f ms\n", gc_get_avg_gc_time_ms());
    printf("  Bytes freed: %llu\n", gc_get_bytes_freed());
    printf("  Regions reclaimed: %llu\n", atomic64_load(&g_gc.stats.regions_reclaimed));
    printf("  Current epoch: %llu\n", g_gc.current_epoch);
    printf("  Threshold: %zu bytes\n", g_gc.threshold_bytes);
    printf("  Pause target: %llu ms\n", g_gc.pause_target_ms);
    printf("  Young gen capacity: %zu bytes\n", g_gc.young_gen_capacity);
    printf("  Survivor capacity: %zu bytes\n", g_gc.survivor_capacity);
    printf("  Old gen capacity: %zu bytes\n", g_gc.old_gen_capacity);
    printf("  GC enabled: %s\n", gc_is_enabled() ? "yes" : "no");
    printf("  Concurrent active: %s\n", gc_is_concurrent_active() ? "yes" : "no");
    printf("  State: %s\n", gc_state_name(g_gc.state));
    printf("  Phase: %s\n", gc_phase_name(g_gc.phase));
    
    gc_region_manager_print_stats();
    return 0;
}

const char* gc_state_name(int state) {
    static const char* names[] = {
        "IDLE",
        "YOUNG_GC",
        "MIXED_GC",
        "FULL_GC",
        "MARKING",
        "COMPACTING"
    };
    
    if (state >= 0 && state < 6) {
        return names[state];
    }
    
    return "UNKNOWN";
}

const char* gc_phase_name(int phase) {
    static const char* names[] = {
        "NONE",
        "MARK_START",
        "MARK_CONCURRENT",
        "MARK_FINAL",
        "MIXED_RECLAIM_START",
        "MIXED_RECLAIM",
        "CLEANUP",
        "IDLE"
    };
    
    if (phase >= 0 && phase < 8) {
        return names[phase];
    }
    
    return "UNKNOWN";
}

void gc_write_barrier(void* ptr) {
    if (!g_gc.initialized || !gc_is_concurrent_active()) return;
    
    struct gc_region* region = gc_region_get_by_addr(ptr);
    if (region) {
        gc_region_mark_object(region, ptr);
    }
}

void gc_write_barrier_pre(void* old_value, void* new_value) {
    if (!g_gc.initialized || !gc_is_concurrent_active()) return;
    
    if (old_value) {
        struct gc_region* region = gc_region_get_by_addr(old_value);
        if (region && gc_region_is_live(region, old_value)) {
            gc_region_mark_object(region, old_value);
        }
    }
}

void gc_register_object(void* ptr, size_t size) {
    if (!g_gc.initialized || !ptr) return;
    
    struct gc_region* region = gc_region_get_by_addr(ptr);
    if (region) {
        spinlock_lock(&region->lock);
        region->allocated_bytes += size;
        spinlock_unlock(&region->lock);
        
        atomic64_add_fetch(&g_gc.stats.young_gen_size, size);
    }
}

void gc_unregister_object(void* ptr) {
    if (!g_gc.initialized || !ptr) return;
    
    struct gc_region* region = gc_region_get_by_addr(ptr);
    if (region) {
        spinlock_lock(&region->lock);
        region->allocated_bytes -= gc_region_get_size(region);
        spinlock_unlock(&region->lock);
    }
}

bool gc_should_collect(void) {
    size_t used_size = g_gc_region_manager.used_size;
    size_t total_size = g_gc_region_manager.total_size;
    
    if (total_size == 0) return false;
    
    double utilization = (double)used_size / (double)total_size;
    
    return utilization >= g_gc.watermark_high;
}

void gc_check_threshold(void) {
    if (gc_should_collect() && gc_is_enabled()) {
        gc_collect();
    }
}

NOMALLOC_EXPORT int gc_get_allocator_stats(struct nomalloc_gc_stats* stats) {
    if (!stats) return -1;
    stats->gc_count = atomic64_load(&g_gc.stats.gc_count);
    stats->gc_time_ns = atomic64_load(&g_gc.stats.gc_time_ns);
    stats->bytes_freed = atomic64_load(&g_gc.stats.bytes_freed);
    stats->regions_freed = atomic64_load(&g_gc.stats.regions_reclaimed);
    stats->pause_avg_ms = 0.0;
    stats->pause_max_ms = 0.0;
    stats->pause_min_ms = 0.0;
    stats->throughput_impact_pct = 0.0;
    return 0;
}

NOMALLOC_EXPORT uint64_t gc_api_get_young_gc_count(void) { return gc_get_young_gc_count(); }
NOMALLOC_EXPORT uint64_t gc_api_get_mixed_gc_count(void) { return gc_get_mixed_gc_count(); }
NOMALLOC_EXPORT uint64_t gc_api_get_full_gc_count(void) { return gc_get_full_gc_count(); }