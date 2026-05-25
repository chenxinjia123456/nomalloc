#ifndef NOMALLOC_STATS_API_H
#define NOMALLOC_STATS_API_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOMALLOC_EXPORT __attribute__((visibility("default")))

NOMALLOC_EXPORT int stats_enable(void);
NOMALLOC_EXPORT int stats_disable(void);
NOMALLOC_EXPORT bool stats_is_enabled(void);

NOMALLOC_EXPORT int stats_get(struct nomalloc_allocator_stats* stats);
NOMALLOC_EXPORT int stats_reset(void);

NOMALLOC_EXPORT void stats_print_summary(void);
NOMALLOC_EXPORT void stats_print_detailed(void);

NOMALLOC_EXPORT int stats_export_json(const char* filename);
NOMALLOC_EXPORT int stats_export_csv(const char* filename);
NOMALLOC_EXPORT int stats_export_binary(const char* filename);

NOMALLOC_EXPORT uint64_t stats_get_total_allocated(void);
NOMALLOC_EXPORT uint64_t stats_get_total_freed(void);
NOMALLOC_EXPORT uint64_t stats_get_active_allocations(void);

NOMALLOC_EXPORT uint64_t stats_get_tcache_hits(void);
NOMALLOC_EXPORT uint64_t stats_get_tcache_misses(void);
NOMALLOC_EXPORT double stats_get_tcache_hit_rate(void);

NOMALLOC_EXPORT double stats_get_fragmentation_ratio(void);
NOMALLOC_EXPORT double stats_get_memory_utilization(void);

NOMALLOC_EXPORT uint64_t stats_get_alloc_ops(void);
NOMALLOC_EXPORT uint64_t stats_get_free_ops(void);
NOMALLOC_EXPORT double stats_get_throughput(void);

NOMALLOC_EXPORT void stats_get_latency_stats(uint64_t* min, uint64_t* max, 
                                              uint64_t* avg, uint64_t* p50, 
                                              uint64_t* p99);

NOMALLOC_EXPORT int stats_get_per_class_stats(uint64_t* allocated_per_class,
                                                uint64_t* freed_per_class,
                                                uint64_t* active_per_class);

NOMALLOC_EXPORT int stats_get_per_arena_stats(uint64_t* allocated_per_arena,
                                               uint64_t* freed_per_arena,
                                               uint64_t* chunks_per_arena);

NOMALLOC_EXPORT int stats_get_per_thread_stats(uint64_t* allocs_per_thread,
                                               uint64_t* frees_per_thread);

#ifdef __cplusplus
}
#endif

#endif