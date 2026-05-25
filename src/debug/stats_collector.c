#include "stats_collector.h"
#include <nomalloc/stats_api.h>
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/time.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include "../core/size_class.h"
#include "../core/allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct stats_collector g_stats_collector;

static void latency_histogram_init(struct latency_histogram* hist) {
    memset(hist, 0, sizeof(struct latency_histogram));
    hist->bucket_size = STATS_LATENCY_BUCKET_SIZE;
    hist->max_buckets = STATS_LATENCY_MAX_BUCKETS;
    hist->min_ns = 0;
    hist->max_ns = 0;
}

static void size_class_stats_init(struct size_class_stats* stats) {
    atomic64_init(&stats->allocations, 0);
    atomic64_init(&stats->deallocations, 0);
    atomic64_init(&stats->allocated_bytes, 0);
    atomic64_init(&stats->freed_bytes, 0);
    atomic64_init(&stats->peak_objects, 0);
    atomic64_init(&stats->peak_bytes, 0);
    
    latency_histogram_init(&stats->alloc_latency);
    latency_histogram_init(&stats->free_latency);
}

static void arena_stats_init(struct arena_stats* stats) {
    atomic64_init(&stats->allocations, 0);
    atomic64_init(&stats->deallocations, 0);
    atomic64_init(&stats->allocated_bytes, 0);
    atomic64_init(&stats->freed_bytes, 0);
    atomic64_init(&stats->chunks, 0);
    atomic64_init(&stats->runs, 0);
    atomic64_init(&stats->tcache_hits, 0);
    atomic64_init(&stats->tcache_misses, 0);
    
    latency_histogram_init(&stats->alloc_latency);
}

static void thread_stats_init(struct thread_stats* stats, uint64_t thread_id) {
    atomic64_init(&stats->allocations, 0);
    atomic64_init(&stats->deallocations, 0);
    atomic64_init(&stats->allocated_bytes, 0);
    atomic64_init(&stats->freed_bytes, 0);
    atomic64_init(&stats->local_cache_hits, 0);
    atomic64_init(&stats->local_cache_misses, 0);
    atomic64_init(&stats->last_alloc_size, 0);
    stats->thread_id = thread_id;
}

static void global_stats_init(struct global_stats* stats) {
    atomic64_init(&stats->total_allocations, 0);
    atomic64_init(&stats->total_deallocations, 0);
    atomic64_init(&stats->total_allocated_bytes, 0);
    atomic64_init(&stats->total_freed_bytes, 0);
    atomic64_init(&stats->total_tcache_hits, 0);
    atomic64_init(&stats->total_tcache_misses, 0);
    atomic64_init(&stats->total_gc_collections, 0);
    atomic64_init(&stats->total_gc_time_ns, 0);
    
    latency_histogram_init(&stats->global_alloc_latency);
    latency_histogram_init(&stats->global_free_latency);
    
    atomic64_init(&stats->start_time_ns, get_time_ns());
    atomic64_init(&stats->last_update_ns, get_time_ns());
    
    stats->throughput_ops_per_sec = 0.0;
    stats->throughput_bytes_per_sec = 0.0;
}

int stats_collector_init(void) {
    if (g_stats_collector.initialized) {
        log_warn("Stats collector already initialized");
        return 0;
    }
    
    memset(&g_stats_collector, 0, sizeof(g_stats_collector));
    
    spinlock_init(&g_stats_collector.global_lock);
    
    global_stats_init(&g_stats_collector.global);
    
    for (size_t i = 0; i < STATS_PER_CLASS_ENTRIES; i++) {
        size_class_stats_init(&g_stats_collector.size_classes[i]);
    }
    
    for (size_t i = 0; i < STATS_PER_ARENA_ENTRIES; i++) {
        arena_stats_init(&g_stats_collector.arenas[i]);
    }
    
    for (size_t i = 0; i < STATS_PER_THREAD_ENTRIES; i++) {
        thread_stats_init(&g_stats_collector.threads[i], 0);
    }
    
    g_stats_collector.history.capacity = STATS_MAX_HISTORY_ENTRIES;
    g_stats_collector.history.count = 0;
    spinlock_init(&g_stats_collector.history.lock);
    
    g_stats_collector.update_interval_ns = 1000000000;
    g_stats_collector.auto_update = true;
    
    g_stats_collector.enabled = true;
    g_stats_collector.initialized = true;
    
    log_info("Stats collector initialized");
    
    return 0;
}

void stats_collector_shutdown(void) {
    if (!g_stats_collector.initialized) {
        return;
    }
    
    spinlock_lock(&g_stats_collector.global_lock);
    
    g_stats_collector.enabled = false;
    g_stats_collector.initialized = false;
    
    spinlock_unlock(&g_stats_collector.global_lock);
    
    log_info("Stats collector shutdown: total_allocations=%llu, total_bytes=%llu",
             atomic64_load(&g_stats_collector.global.total_allocations),
             atomic64_load(&g_stats_collector.global.total_allocated_bytes));
}

int stats_collector_enable(void) {
    if (!g_stats_collector.initialized) {
        return stats_collector_init();
    }
    
    g_stats_collector.enabled = true;
    log_debug("Stats collector enabled");
    return 0;
}

int stats_collector_disable(void) {
    if (!g_stats_collector.initialized) return -1;
    g_stats_collector.enabled = false;
    log_debug("Stats collector disabled");
    return 0;
}

bool stats_collector_is_enabled(void) {
    return g_stats_collector.enabled;
}

void stats_collector_record_alloc(size_t size, size_t class_idx, int arena_id, 
                                  uint64_t thread_id, uint64_t latency_ns) {
    if (!g_stats_collector.enabled) return;
    
    atomic64_inc(&g_stats_collector.global.total_allocations);
    atomic64_add_fetch(&g_stats_collector.global.total_allocated_bytes, size);
    
    latency_histogram_add(&g_stats_collector.global.global_alloc_latency, latency_ns);
    
    if (class_idx < STATS_PER_CLASS_ENTRIES) {
        atomic64_inc(&g_stats_collector.size_classes[class_idx].allocations);
        atomic64_add_fetch(&g_stats_collector.size_classes[class_idx].allocated_bytes, size);
        latency_histogram_add(&g_stats_collector.size_classes[class_idx].alloc_latency, latency_ns);
        
        uint64_t active = atomic64_load(&g_stats_collector.size_classes[class_idx].allocations) -
                         atomic64_load(&g_stats_collector.size_classes[class_idx].deallocations);
        uint64_t peak = atomic64_load(&g_stats_collector.size_classes[class_idx].peak_objects);
        if (active > peak) {
            atomic64_store(&g_stats_collector.size_classes[class_idx].peak_objects, active);
        }
    }
    
    if (arena_id >= 0 && arena_id < STATS_PER_ARENA_ENTRIES) {
        atomic64_inc(&g_stats_collector.arenas[arena_id].allocations);
        atomic64_add_fetch(&g_stats_collector.arenas[arena_id].allocated_bytes, size);
        latency_histogram_add(&g_stats_collector.arenas[arena_id].alloc_latency, latency_ns);
    }
}

void stats_collector_record_dealloc(size_t size, size_t class_idx, int arena_id,
                                    uint64_t thread_id, uint64_t latency_ns) {
    if (!g_stats_collector.enabled) return;
    
    atomic64_inc(&g_stats_collector.global.total_deallocations);
    atomic64_add_fetch(&g_stats_collector.global.total_freed_bytes, size);
    
    latency_histogram_add(&g_stats_collector.global.global_free_latency, latency_ns);
    
    if (class_idx < STATS_PER_CLASS_ENTRIES) {
        atomic64_inc(&g_stats_collector.size_classes[class_idx].deallocations);
        atomic64_add_fetch(&g_stats_collector.size_classes[class_idx].freed_bytes, size);
        latency_histogram_add(&g_stats_collector.size_classes[class_idx].free_latency, latency_ns);
    }
    
    if (arena_id >= 0 && arena_id < STATS_PER_ARENA_ENTRIES) {
        atomic64_inc(&g_stats_collector.arenas[arena_id].deallocations);
        atomic64_add_fetch(&g_stats_collector.arenas[arena_id].freed_bytes, size);
    }
}

void stats_collector_record_tcache_hit(size_t class_idx, uint64_t thread_id) {
    if (!g_stats_collector.enabled) return;
    
    atomic64_inc(&g_stats_collector.global.total_tcache_hits);
}

void stats_collector_record_tcache_miss(size_t class_idx, uint64_t thread_id) {
    if (!g_stats_collector.enabled) return;
    
    atomic64_inc(&g_stats_collector.global.total_tcache_misses);
}

void stats_collector_record_gc(uint64_t time_ns, uint64_t bytes_freed) {
    if (!g_stats_collector.enabled) return;
    
    atomic64_inc(&g_stats_collector.global.total_gc_collections);
    atomic64_add_fetch(&g_stats_collector.global.total_gc_time_ns, time_ns);
}

void stats_collector_update_global(void) {
    uint64_t now = get_time_ns();
    uint64_t start = atomic64_load(&g_stats_collector.global.start_time_ns);
    uint64_t elapsed = now - start;
    
    uint64_t total_ops = atomic64_load(&g_stats_collector.global.total_allocations) +
                        atomic64_load(&g_stats_collector.global.total_deallocations);
    
    if (elapsed > 0) {
        g_stats_collector.global.throughput_ops_per_sec = 
            (double)total_ops / (elapsed / 1e9);
        g_stats_collector.global.throughput_bytes_per_sec = 
            (double)atomic64_load(&g_stats_collector.global.total_allocated_bytes) / 
            (elapsed / 1e9);
    }
    
    atomic64_store(&g_stats_collector.global.last_update_ns, now);
}

void stats_collector_update_history(void) {
    spinlock_lock(&g_stats_collector.history.lock);
    
    if (g_stats_collector.history.count >= g_stats_collector.history.capacity) {
        for (size_t i = 0; i < g_stats_collector.history.capacity - 1; i++) {
            g_stats_collector.history.entries[i] = 
                g_stats_collector.history.entries[i + 1];
        }
        g_stats_collector.history.count = g_stats_collector.history.capacity - 1;
    }
    
    struct stats_history_entry* entry = 
        &g_stats_collector.history.entries[g_stats_collector.history.count++];
    
    entry->timestamp = get_time_ns();
    entry->total_allocated = atomic64_load(&g_stats_collector.global.total_allocated_bytes);
    entry->total_freed = atomic64_load(&g_stats_collector.global.total_freed_bytes);
    entry->active_allocations = entry->total_allocated - entry->total_freed;
    
    stats_collector_update_global();
    entry->throughput_ops = g_stats_collector.global.throughput_ops_per_sec;
    
    spinlock_unlock(&g_stats_collector.history.lock);
}

int stats_collector_get_global(struct nomalloc_allocator_stats* stats) {
    if (!stats) return -1;
    
    stats_collector_update_global();
    
    stats->total_allocated = atomic64_load(&g_stats_collector.global.total_allocated_bytes);
    stats->total_freed = atomic64_load(&g_stats_collector.global.total_freed_bytes);
    stats->active_allocations = stats->total_allocated - stats->total_freed;
    
    stats->tcache_hits = atomic64_load(&g_stats_collector.global.total_tcache_hits);
    stats->tcache_misses = atomic64_load(&g_stats_collector.global.total_tcache_misses);
    
    uint64_t hits = stats->tcache_hits;
    uint64_t misses = stats->tcache_misses;
    stats->tcache_hit_rate = (hits + misses > 0) ? 
        (double)hits / (double)(hits + misses) : 0.0;
    
    stats->alloc_ops = atomic64_load(&g_stats_collector.global.total_allocations);
    stats->free_ops = atomic64_load(&g_stats_collector.global.total_deallocations);
    stats->throughput_ops_per_sec = g_stats_collector.global.throughput_ops_per_sec;
    
    stats->alloc_latency_min = g_stats_collector.global.global_alloc_latency.min_ns;
    stats->alloc_latency_max = g_stats_collector.global.global_alloc_latency.max_ns;
    stats->alloc_latency_avg = latency_histogram_get_avg(&g_stats_collector.global.global_alloc_latency);
    stats->alloc_latency_p50 = latency_histogram_get_p50(&g_stats_collector.global.global_alloc_latency);
    stats->alloc_latency_p99 = latency_histogram_get_p99(&g_stats_collector.global.global_alloc_latency);
    
    stats->gc_count = atomic64_load(&g_stats_collector.global.total_gc_collections);
    stats->gc_time_ns = atomic64_load(&g_stats_collector.global.total_gc_time_ns);
    
    return 0;
}

int stats_collector_get_per_class(uint64_t* allocated_per_class, 
                                  uint64_t* freed_per_class, 
                                  uint64_t* active_per_class) {
    if (!allocated_per_class || !freed_per_class || !active_per_class) return -1;
    
    for (size_t i = 0; i < STATS_PER_CLASS_ENTRIES; i++) {
        allocated_per_class[i] = atomic64_load(&g_stats_collector.size_classes[i].allocated_bytes);
        freed_per_class[i] = atomic64_load(&g_stats_collector.size_classes[i].freed_bytes);
        active_per_class[i] = allocated_per_class[i] - freed_per_class[i];
    }
    
    return 0;
}

int stats_collector_get_per_arena(uint64_t* allocated_per_arena,
                                  uint64_t* freed_per_arena,
                                  uint64_t* chunks_per_arena) {
    if (!allocated_per_arena || !freed_per_arena || !chunks_per_arena) return -1;
    
    for (size_t i = 0; i < STATS_PER_ARENA_ENTRIES; i++) {
        allocated_per_arena[i] = atomic64_load(&g_stats_collector.arenas[i].allocated_bytes);
        freed_per_arena[i] = atomic64_load(&g_stats_collector.arenas[i].freed_bytes);
        chunks_per_arena[i] = atomic64_load(&g_stats_collector.arenas[i].chunks);
    }
    
    return 0;
}

void stats_collector_print_summary(void) {
    struct nomalloc_allocator_stats stats;
    stats_collector_get_global(&stats);
    
    printf("\nAllocator Statistics Summary:\n");
    printf("  Total allocated: %llu bytes (%.2f MB)\n", 
           stats.total_allocated, stats.total_allocated / (1024.0 * 1024.0));
    printf("  Total freed: %llu bytes (%.2f MB)\n", 
           stats.total_freed, stats.total_freed / (1024.0 * 1024.0));
    printf("  Active: %llu bytes (%.2f MB)\n", 
           stats.active_allocations, stats.active_allocations / (1024.0 * 1024.0));
    printf("  Alloc ops: %llu\n", stats.alloc_ops);
    printf("  Free ops: %llu\n", stats.free_ops);
    printf("  Throughput: %.2f ops/sec, %.2f bytes/sec\n", 
           stats.throughput_ops_per_sec, g_stats_collector.global.throughput_bytes_per_sec);
    printf("  Tcache hits: %llu\n", stats.tcache_hits);
    printf("  Tcache misses: %llu\n", stats.tcache_misses);
    printf("  Tcache hit rate: %.2f%%\n", stats.tcache_hit_rate * 100);
    printf("  Alloc latency (avg/p50/p99): %llu/%llu/%llu ns\n", 
           stats.alloc_latency_avg, stats.alloc_latency_p50, stats.alloc_latency_p99);
    printf("  GC count: %llu\n", stats.gc_count);
    printf("  GC time: %llu ns\n", stats.gc_time_ns);
}

void stats_collector_print_detailed(void) {
    stats_collector_print_summary();
    stats_collector_print_size_class_stats();
    stats_collector_print_arena_stats();
}

void stats_collector_print_size_class_stats(void) {
    printf("\nSize Class Statistics:\n");
    
    for (size_t i = 0; i < STATS_PER_CLASS_ENTRIES; i++) {
        uint64_t allocs = atomic64_load(&g_stats_collector.size_classes[i].allocations);
        uint64_t deallocs = atomic64_load(&g_stats_collector.size_classes[i].deallocations);
        
        if (allocs > 0 || deallocs > 0) {
            size_t size = class_to_size(i);
            printf("  Class %zu (size=%zu): allocs=%llu, deallocs=%llu, peak=%llu\n",
                   i, size, allocs, deallocs,
                   atomic64_load(&g_stats_collector.size_classes[i].peak_objects));
        }
    }
}

void stats_collector_print_arena_stats(void) {
    printf("\nArena Statistics:\n");
    
    for (size_t i = 0; i < STATS_PER_ARENA_ENTRIES; i++) {
        uint64_t allocs = atomic64_load(&g_stats_collector.arenas[i].allocations);
        uint64_t deallocs = atomic64_load(&g_stats_collector.arenas[i].deallocations);
        
        if (allocs > 0 || deallocs > 0) {
            printf("  Arena %zu: allocs=%llu, deallocs=%llu, chunks=%llu\n",
                   i, allocs, deallocs,
                   atomic64_load(&g_stats_collector.arenas[i].chunks));
        }
    }
}

int stats_collector_reset(void) {
    spinlock_lock(&g_stats_collector.global_lock);
    
    global_stats_init(&g_stats_collector.global);
    
    for (size_t i = 0; i < STATS_PER_CLASS_ENTRIES; i++) {
        size_class_stats_init(&g_stats_collector.size_classes[i]);
    }
    
    for (size_t i = 0; i < STATS_PER_ARENA_ENTRIES; i++) {
        arena_stats_init(&g_stats_collector.arenas[i]);
    }
    
    g_stats_collector.history.count = 0;
    
    spinlock_unlock(&g_stats_collector.global_lock);
    
    log_debug("Stats collector reset");
    return 0;
}

int stats_collector_export_json(const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        log_error("Failed to open stats export file: %s", filename);
        return -1;
    }
    
    struct nomalloc_allocator_stats stats;
    stats_collector_get_global(&stats);
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"total_allocated\": %llu,\n", stats.total_allocated);
    fprintf(fp, "  \"total_freed\": %llu,\n", stats.total_freed);
    fprintf(fp, "  \"active_allocations\": %llu,\n", stats.active_allocations);
    fprintf(fp, "  \"tcache_hits\": %llu,\n", stats.tcache_hits);
    fprintf(fp, "  \"tcache_misses\": %llu,\n", stats.tcache_misses);
    fprintf(fp, "  \"tcache_hit_rate\": %.4f,\n", stats.tcache_hit_rate);
    fprintf(fp, "  \"alloc_ops\": %llu,\n", stats.alloc_ops);
    fprintf(fp, "  \"free_ops\": %llu,\n", stats.free_ops);
    fprintf(fp, "  \"throughput_ops_per_sec\": %.2f,\n", stats.throughput_ops_per_sec);
    fprintf(fp, "  \"alloc_latency_avg\": %llu,\n", stats.alloc_latency_avg);
    fprintf(fp, "  \"alloc_latency_p99\": %llu,\n", stats.alloc_latency_p99);
    fprintf(fp, "  \"gc_count\": %llu,\n", stats.gc_count);
    fprintf(fp, "  \"gc_time_ns\": %llu\n", stats.gc_time_ns);
    fprintf(fp, "}\n");
    
    fclose(fp);
    
    log_info("Stats exported to JSON: %s", filename);
    return 0;
}

int stats_collector_export_csv(const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        log_error("Failed to open stats export file: %s", filename);
        return -1;
    }
    
    fprintf(fp, "metric,value\n");
    
    struct nomalloc_allocator_stats stats;
    stats_collector_get_global(&stats);
    
    fprintf(fp, "total_allocated,%llu\n", stats.total_allocated);
    fprintf(fp, "total_freed,%llu\n", stats.total_freed);
    fprintf(fp, "active_allocations,%llu\n", stats.active_allocations);
    fprintf(fp, "tcache_hits,%llu\n", stats.tcache_hits);
    fprintf(fp, "tcache_misses,%llu\n", stats.tcache_misses);
    fprintf(fp, "tcache_hit_rate,%f\n", stats.tcache_hit_rate);
    fprintf(fp, "alloc_ops,%llu\n", stats.alloc_ops);
    fprintf(fp, "free_ops,%llu\n", stats.free_ops);
    fprintf(fp, "throughput_ops_per_sec,%f\n", stats.throughput_ops_per_sec);
    fprintf(fp, "alloc_latency_avg,%llu\n", stats.alloc_latency_avg);
    fprintf(fp, "alloc_latency_p99,%llu\n", stats.alloc_latency_p99);
    fprintf(fp, "gc_count,%llu\n", stats.gc_count);
    fprintf(fp, "gc_time_ns,%llu\n", stats.gc_time_ns);
    
    fclose(fp);
    
    log_info("Stats exported to CSV: %s", filename);
    return 0;
}

uint64_t stats_collector_get_free_ops(void) {
    return atomic64_load(&g_stats_collector.global.total_deallocations);
}

double stats_collector_get_throughput(void) {
    return g_stats_collector.global.throughput_ops_per_sec;
}

void stats_collector_get_latency_stats(uint64_t* min, uint64_t* max, uint64_t* avg, uint64_t* p50, uint64_t* p99) {
    if (min) *min = g_stats_collector.global.global_alloc_latency.min_ns;
    if (max) *max = g_stats_collector.global.global_alloc_latency.max_ns;
    if (avg) {
        uint64_t samples = g_stats_collector.global.global_alloc_latency.total_samples;
        if (samples > 0) {
            *avg = g_stats_collector.global.global_alloc_latency.sum_ns / samples;
        } else {
            *avg = 0;
        }
    }
    if (p50) *p50 = latency_histogram_get_p50(&g_stats_collector.global.global_alloc_latency);
    if (p99) *p99 = latency_histogram_get_p99(&g_stats_collector.global.global_alloc_latency);
}

int stats_collector_export_binary(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        log_error("Failed to open binary file: %s", filename);
        return -1;
    }
    
    fwrite(&g_stats_collector.global, sizeof(struct nomalloc_allocator_stats), 1, fp);
    fclose(fp);
    
    log_info("Stats exported to binary: %s", filename);
    return 0;
}

uint64_t stats_collector_get_total_allocated(void) {
    return atomic64_load(&g_stats_collector.global.total_allocated_bytes);
}

uint64_t stats_collector_get_total_freed(void) {
    return atomic64_load(&g_stats_collector.global.total_freed_bytes);
}

uint64_t stats_collector_get_active_allocations(void) {
    return atomic64_load(&g_stats_collector.global.total_allocated_bytes) -
           atomic64_load(&g_stats_collector.global.total_freed_bytes);
}

uint64_t stats_collector_get_tcache_hits(void) {
    return atomic64_load(&g_stats_collector.global.total_tcache_hits);
}

uint64_t stats_collector_get_tcache_misses(void) {
    return atomic64_load(&g_stats_collector.global.total_tcache_misses);
}

double stats_collector_get_tcache_hit_rate(void) {
    uint64_t hits = atomic64_load(&g_stats_collector.global.total_tcache_hits);
    uint64_t misses = atomic64_load(&g_stats_collector.global.total_tcache_misses);
    if (hits + misses == 0) return 0.0;
    return (double)hits / (double)(hits + misses);
}

double stats_collector_get_fragmentation_ratio(void) {
    return 0.0;
}

double stats_collector_get_memory_utilization(void) {
    uint64_t allocated = atomic64_load(&g_stats_collector.global.total_allocated_bytes);
    uint64_t freed = atomic64_load(&g_stats_collector.global.total_freed_bytes);
    uint64_t active = allocated - freed;
    if (allocated == 0) return 0.0;
    return (double)active / (double)allocated;
}

uint64_t stats_collector_get_alloc_ops(void) {
    return atomic64_load(&g_stats_collector.global.total_allocations);
}

NOMALLOC_EXPORT int stats_enable(void) { return stats_collector_enable(); }
NOMALLOC_EXPORT int stats_disable(void) { return stats_collector_disable(); }
NOMALLOC_EXPORT bool stats_is_enabled(void) { return stats_collector_is_enabled(); }
NOMALLOC_EXPORT int stats_get(struct nomalloc_allocator_stats* stats) { return stats_collector_get_global(stats); }
NOMALLOC_EXPORT int stats_reset(void) { return stats_collector_reset(); }
NOMALLOC_EXPORT void stats_print_summary(void) { stats_collector_print_summary(); }
NOMALLOC_EXPORT void stats_print_detailed(void) { stats_collector_print_detailed(); }
NOMALLOC_EXPORT int stats_export_json(const char* filename) { return stats_collector_export_json(filename); }
NOMALLOC_EXPORT int stats_export_csv(const char* filename) { return stats_collector_export_csv(filename); }
NOMALLOC_EXPORT int stats_export_binary(const char* filename) { return stats_collector_export_binary(filename); }
NOMALLOC_EXPORT uint64_t stats_get_total_allocated(void) { return stats_collector_get_total_allocated(); }
NOMALLOC_EXPORT uint64_t stats_get_total_freed(void) { return stats_collector_get_total_freed(); }
NOMALLOC_EXPORT uint64_t stats_get_active_allocations(void) { return stats_collector_get_active_allocations(); }
NOMALLOC_EXPORT uint64_t stats_get_tcache_hits(void) { return stats_collector_get_tcache_hits(); }
NOMALLOC_EXPORT uint64_t stats_get_tcache_misses(void) { return stats_collector_get_tcache_misses(); }
NOMALLOC_EXPORT double stats_get_tcache_hit_rate(void) { return stats_collector_get_tcache_hit_rate(); }
NOMALLOC_EXPORT double stats_get_fragmentation_ratio(void) { return stats_collector_get_fragmentation_ratio(); }
NOMALLOC_EXPORT double stats_get_memory_utilization(void) { return stats_collector_get_memory_utilization(); }
NOMALLOC_EXPORT uint64_t stats_get_alloc_ops(void) { return stats_collector_get_alloc_ops(); }
NOMALLOC_EXPORT uint64_t stats_get_free_ops(void) { return stats_collector_get_free_ops(); }
NOMALLOC_EXPORT double stats_get_throughput(void) { return stats_collector_get_throughput(); }
NOMALLOC_EXPORT void stats_get_latency_stats(uint64_t* min, uint64_t* max, uint64_t* avg, uint64_t* p50, uint64_t* p99) {
    stats_collector_get_latency_stats(min, max, avg, p50, p99);
}
NOMALLOC_EXPORT int stats_get_per_class_stats(uint64_t* allocated, uint64_t* freed, uint64_t* active) {
    return stats_collector_get_per_class(allocated, freed, active);
}
NOMALLOC_EXPORT int stats_get_per_arena_stats(uint64_t* allocated, uint64_t* freed, uint64_t* active) {
    return stats_collector_get_per_arena(allocated, freed, active);
}