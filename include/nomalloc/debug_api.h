#ifndef NOMALLOC_DEBUG_API_H
#define NOMALLOC_DEBUG_API_H

#include "types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOMALLOC_EXPORT __attribute__((visibility("default")))

NOMALLOC_EXPORT int leak_detector_enable(void);
NOMALLOC_EXPORT int leak_detector_disable(void);
NOMALLOC_EXPORT bool leak_detector_is_enabled(void);

NOMALLOC_EXPORT int leak_detector_start_tracking(void);
NOMALLOC_EXPORT int leak_detector_stop_tracking(void);

NOMALLOC_EXPORT int leak_detector_get_stats(struct nomalloc_leak_stats* stats);
NOMALLOC_EXPORT int leak_detector_generate_report(const char* filename);
NOMALLOC_EXPORT int leak_detector_print_report(void);
NOMALLOC_EXPORT int leak_detector_clear(void);

NOMALLOC_EXPORT int profiler_enable(double sample_rate);
NOMALLOC_EXPORT int profiler_disable(void);
NOMALLOC_EXPORT bool profiler_is_enabled(void);

NOMALLOC_EXPORT int profiler_get_sample_rate(double* rate);
NOMALLOC_EXPORT int profiler_set_sample_rate(double rate);

NOMALLOC_EXPORT int profiler_analyze_hotspots(void);
NOMALLOC_EXPORT int profiler_get_top_alloc_sizes(size_t n, size_t* sizes);
NOMALLOC_EXPORT int profiler_get_top_latencies(size_t n, uint64_t* latencies);
NOMALLOC_EXPORT int profiler_get_top_threads(size_t n, uint64_t* thread_ids);

NOMALLOC_EXPORT int profiler_generate_report(const char* filename);
NOMALLOC_EXPORT int profiler_print_report(void);
NOMALLOC_EXPORT int profiler_clear(void);

NOMALLOC_EXPORT int tracer_enable(void);
NOMALLOC_EXPORT int tracer_disable(void);
NOMALLOC_EXPORT bool tracer_is_enabled(void);

NOMALLOC_EXPORT int tracer_get_trace_count(uint64_t* count);
NOMALLOC_EXPORT int tracer_get_recent_traces(size_t n, struct trace_entry* entries);
NOMALLOC_EXPORT int tracer_clear(void);

struct trace_entry {
    uint64_t timestamp;
    uint64_t thread_id;
    int operation;
    void* address;
    size_t size;
    size_t class_idx;
    int arena_id;
    uint64_t latency_ns;
};

#define TRACE_OP_ALLOC  1
#define TRACE_OP_FREE   2
#define TRACE_OP_REALLOC 3
#define TRACE_OP_GC_START 4
#define TRACE_OP_GC_END  5

NOMALLOC_EXPORT void debug_set_verbose(bool verbose);
NOMALLOC_EXPORT bool debug_is_verbose(void);

NOMALLOC_EXPORT int debug_print_allocator_state(void);
NOMALLOC_EXPORT int debug_print_tcache_state(void);
NOMALLOC_EXPORT int debug_print_arena_state(int arena_id);
NOMALLOC_EXPORT int debug_print_gc_state(void);

NOMALLOC_EXPORT int debug_validate_memory(void);
NOMALLOC_EXPORT int debug_check_corruption(void);

NOMALLOC_EXPORT int debug_dump_memory_map(const char* filename);
NOMALLOC_EXPORT int debug_dump_allocation_history(const char* filename, size_t n);

#ifdef __cplusplus
}
#endif

#endif