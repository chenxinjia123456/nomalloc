#ifndef NOMALLOC_TYPES_H
#define NOMALLOC_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOMALLOC_VERSION_MAJOR 0
#define NOMALLOC_VERSION_MINOR 1
#define NOMALLOC_VERSION_PATCH 0
#define NOMALLOC_VERSION "0.1.0"

#define CACHE_LINE_SIZE 64

#define alignas_cache_line alignas(CACHE_LINE_SIZE)

typedef atomic_uint_fast64_t nomalloc_atomic64_t;
typedef atomic_uint_fast32_t nomalloc_atomic32_t;
typedef atomic_int_fast8_t nomalloc_atomic8_t;

struct nomalloc_allocator_stats {
    uint64_t total_allocated;
    uint64_t total_freed;
    uint64_t active_allocations;
    
    uint64_t tcache_hits;
    uint64_t tcache_misses;
    double tcache_hit_rate;
    
    uint64_t alloc_ops;
    uint64_t free_ops;
    double throughput_ops_per_sec;
    
    uint64_t alloc_latency_min;
    uint64_t alloc_latency_max;
    uint64_t alloc_latency_avg;
    uint64_t alloc_latency_p50;
    uint64_t alloc_latency_p99;
    
    double fragmentation_ratio;
    double memory_utilization;
    
    uint64_t gc_count;
    uint64_t gc_time_ns;
    uint64_t gc_bytes_freed;
    double gc_pause_avg_ms;
    double gc_pause_max_ms;
};

struct nomalloc_gc_stats {
    uint64_t gc_count;
    uint64_t gc_time_ns;
    uint64_t bytes_freed;
    uint64_t regions_freed;
    
    double pause_avg_ms;
    double pause_max_ms;
    double pause_min_ms;
    
    double throughput_impact_pct;
};

struct nomalloc_leak_stats {
    uint64_t leaked_objects;
    uint64_t leaked_bytes;
    uint64_t total_allocated;
    uint64_t total_freed;
    uint64_t active_allocations;
};

struct nomalloc_allocator_config {
    size_t region_size;
    size_t tcache_max_size;
    size_t gc_threshold;
    double gc_watermark_high;
    double gc_watermark_low;
    bool numa_aware;
    bool huge_pages;
    int log_level;
    bool leak_detection_enabled;
    bool stats_enabled;
    bool profiler_enabled;
    double profiler_sample_rate;
};

#define NOMALLOC_LOG_LEVEL_NONE  0
#define NOMALLOC_LOG_LEVEL_ERROR 1
#define NOMALLOC_LOG_LEVEL_WARN  2
#define NOMALLOC_LOG_LEVEL_INFO  3
#define NOMALLOC_LOG_LEVEL_DEBUG 4
#define NOMALLOC_LOG_LEVEL_TRACE 5

#define NOMALLOC_REGION_SIZE_DEFAULT (2 * 1024 * 1024)
#define NOMALLOC_REGION_SIZE_MIN     (4 * 1024)
#define NOMALLOC_REGION_SIZE_MAX     (256 * 1024 * 1024)

#define NOMALLOC_TCACHE_SIZE_DEFAULT (1024 * 1024)
#define NOMALLOC_TCACHE_SIZE_MIN     (64 * 1024)

#define NOMALLOC_GC_THRESHOLD_DEFAULT (1024 * 1024 * 1024)
#define NOMALLOC_GC_WATERMARK_HIGH_DEFAULT 0.8
#define NOMALLOC_GC_WATERMARK_LOW_DEFAULT  0.6

#define NOMALLOC_NUM_SIZE_CLASSES_SMALL  30
#define NOMALLOC_NUM_SIZE_CLASSES_MEDIUM 17
#define NOMALLOC_NUM_SIZE_CLASSES_TOTAL  (NOMALLOC_NUM_SIZE_CLASSES_SMALL + NOMALLOC_NUM_SIZE_CLASSES_MEDIUM)

#define NOMALLOC_SMALL_SIZE_MAX  (4 * 1024)
#define NOMALLOC_MEDIUM_SIZE_MAX (1 * 1024 * 1024)

#define NOMALLOC_MAX_ARENAS  64
#define NOMALLOC_MAX_THREADS 1024

#define NOMALLOC_SUCCESS 0
#define NOMALLOC_ERROR_INVALID_PARAM   -1
#define NOMALLOC_ERROR_NO_MEMORY       -2
#define NOMALLOC_ERROR_NOT_INITIALIZED -3
#define NOMALLOC_ERROR_ALREADY_INITIALIZED -4
#define NOMALLOC_ERROR_THREAD_ERROR    -5
#define NOMALLOC_ERROR_LOCK_ERROR      -6
#define NOMALLOC_ERROR_GC_ERROR        -7

#ifdef __cplusplus
}
#endif

#endif