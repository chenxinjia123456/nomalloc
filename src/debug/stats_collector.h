#ifndef NOMALLOC_DEBUG_STATS_COLLECTOR_H
#define NOMALLOC_DEBUG_STATS_COLLECTOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../utils/atomic.h"
#include "../utils/spinlock.h"
#include "../utils/list.h"
#include <nomalloc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATS_MAX_HISTORY_ENTRIES 10000
#define STATS_PER_CLASS_ENTRIES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#define STATS_PER_ARENA_ENTRIES NOMALLOC_MAX_ARENAS
#define STATS_PER_THREAD_ENTRIES NOMALLOC_MAX_THREADS

#define STATS_LATENCY_BUCKET_SIZE 16
#define STATS_LATENCY_MAX_BUCKETS 64

struct latency_histogram {
    uint64_t buckets[STATS_LATENCY_MAX_BUCKETS];
    size_t bucket_size;
    size_t max_buckets;
    
    uint64_t total_samples;
    uint64_t sum_ns;
    
    uint64_t min_ns;
    uint64_t max_ns;
};

struct size_class_stats {
    atomic64_t allocations;
    atomic64_t deallocations;
    atomic64_t allocated_bytes;
    atomic64_t freed_bytes;
    
    struct latency_histogram alloc_latency;
    struct latency_histogram free_latency;
    
    atomic64_t peak_objects;
    atomic64_t peak_bytes;
};

struct arena_stats {
    atomic64_t allocations;
    atomic64_t deallocations;
    atomic64_t allocated_bytes;
    atomic64_t freed_bytes;
    
    atomic64_t chunks;
    atomic64_t runs;
    
    atomic64_t tcache_hits;
    atomic64_t tcache_misses;
    
    struct latency_histogram alloc_latency;
};

struct thread_stats {
    atomic64_t allocations;
    atomic64_t deallocations;
    atomic64_t allocated_bytes;
    atomic64_t freed_bytes;
    
    atomic64_t local_cache_hits;
    atomic64_t local_cache_misses;
    
    atomic64_t last_alloc_size;
    uint64_t thread_id;
};

struct global_stats {
    atomic64_t total_allocations;
    atomic64_t total_deallocations;
    atomic64_t total_allocated_bytes;
    atomic64_t total_freed_bytes;
    
    atomic64_t total_tcache_hits;
    atomic64_t total_tcache_misses;
    atomic64_t total_gc_collections;
    atomic64_t total_gc_time_ns;
    
    struct latency_histogram global_alloc_latency;
    struct latency_histogram global_free_latency;
    
    atomic64_t start_time_ns;
    atomic64_t last_update_ns;
    
    double throughput_ops_per_sec;
    double throughput_bytes_per_sec;
};

struct stats_history_entry {
    uint64_t timestamp;
    uint64_t total_allocated;
    uint64_t total_freed;
    uint64_t active_allocations;
    
    double fragmentation_ratio;
    double utilization;
    
    double throughput_ops;
};

struct stats_history {
    struct stats_history_entry entries[STATS_MAX_HISTORY_ENTRIES];
    size_t count;
    size_t capacity;
    
    spinlock_t lock;
};

struct stats_collector {
    bool enabled;
    bool initialized;
    
    struct global_stats global;
    struct size_class_stats size_classes[STATS_PER_CLASS_ENTRIES];
    struct arena_stats arenas[STATS_PER_ARENA_ENTRIES];
    struct thread_stats threads[STATS_PER_THREAD_ENTRIES];
    
    struct stats_history history;
    
    uint64_t update_interval_ns;
    bool auto_update;
    
    spinlock_t global_lock;
};

extern struct stats_collector g_stats_collector;

int stats_collector_init(void);
void stats_collector_shutdown(void);

int stats_collector_enable(void);
int stats_collector_disable(void);
bool stats_collector_is_enabled(void);

void stats_collector_record_alloc(size_t size, size_t class_idx, int arena_id, 
                                  uint64_t thread_id, uint64_t latency_ns);
void stats_collector_record_dealloc(size_t size, size_t class_idx, int arena_id,
                                    uint64_t thread_id, uint64_t latency_ns);
void stats_collector_record_tcache_hit(size_t class_idx, uint64_t thread_id);
void stats_collector_record_tcache_miss(size_t class_idx, uint64_t thread_id);
void stats_collector_record_gc(uint64_t time_ns, uint64_t bytes_freed);

void stats_collector_update_global(void);
void stats_collector_update_history(void);

int stats_collector_get_global(struct nomalloc_allocator_stats* stats);
int stats_collector_get_per_class(uint64_t* allocated_per_class, 
                                  uint64_t* freed_per_class, 
                                  uint64_t* active_per_class);
int stats_collector_get_per_arena(uint64_t* allocated_per_arena,
                                  uint64_t* freed_per_arena,
                                  uint64_t* chunks_per_arena);
int stats_collector_get_per_thread(uint64_t* allocs_per_thread,
                                   uint64_t* frees_per_thread);

void stats_collector_print_summary(void);
void stats_collector_print_detailed(void);
void stats_collector_print_size_class_stats(void);
void stats_collector_print_arena_stats(void);
void stats_collector_print_thread_stats(void);

int stats_collector_export_json(const char* filename);
int stats_collector_export_csv(const char* filename);

int stats_collector_reset(void);

static inline void latency_histogram_add(struct latency_histogram* hist, uint64_t value) {
    size_t bucket_idx = value / hist->bucket_size;
    if (bucket_idx >= hist->max_buckets) {
        bucket_idx = hist->max_buckets - 1;
    }
    
    hist->buckets[bucket_idx]++;
    hist->total_samples++;
    hist->sum_ns += value;
    
    if (value < hist->min_ns || hist->min_ns == 0) {
        hist->min_ns = value;
    }
    if (value > hist->max_ns) {
        hist->max_ns = value;
    }
}

static inline uint64_t latency_histogram_get_avg(struct latency_histogram* hist) {
    if (hist->total_samples == 0) return 0;
    return hist->sum_ns / hist->total_samples;
}

static inline uint64_t latency_histogram_get_p50(struct latency_histogram* hist) {
    if (hist->total_samples == 0) return 0;
    
    uint64_t target = hist->total_samples / 2;
    uint64_t cumulative = 0;
    
    for (size_t i = 0; i < hist->max_buckets; i++) {
        cumulative += hist->buckets[i];
        if (cumulative >= target) {
            return i * hist->bucket_size;
        }
    }
    
    return hist->max_ns;
}

static inline uint64_t latency_histogram_get_p99(struct latency_histogram* hist) {
    if (hist->total_samples == 0) return 0;
    
    uint64_t target = hist->total_samples * 99 / 100;
    uint64_t cumulative = 0;
    
    for (size_t i = 0; i < hist->max_buckets; i++) {
        cumulative += hist->buckets[i];
        if (cumulative >= target) {
            return i * hist->bucket_size;
        }
    }
    
    return hist->max_ns;
}

#ifdef __cplusplus
}
#endif

#endif