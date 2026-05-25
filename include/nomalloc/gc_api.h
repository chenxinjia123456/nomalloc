#ifndef NOMALLOC_GC_API_H
#define NOMALLOC_GC_API_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOMALLOC_EXPORT __attribute__((visibility("default")))

NOMALLOC_EXPORT int gc_collect(void);
NOMALLOC_EXPORT int gc_collect_async(void);
NOMALLOC_EXPORT int gc_collect_wait(void);

NOMALLOC_EXPORT int gc_enable(void);
NOMALLOC_EXPORT int gc_disable(void);
NOMALLOC_EXPORT bool gc_is_enabled(void);

NOMALLOC_EXPORT int gc_set_threshold(size_t threshold);
NOMALLOC_EXPORT size_t gc_get_threshold(void);

NOMALLOC_EXPORT int gc_set_pause_target(uint64_t target_ms);
NOMALLOC_EXPORT uint64_t gc_get_pause_target(void);

NOMALLOC_EXPORT int gc_collect_young(void);
NOMALLOC_EXPORT int gc_collect_mixed(void);
NOMALLOC_EXPORT int gc_collect_full(void);

NOMALLOC_EXPORT uint64_t gc_get_young_gc_count(void);
NOMALLOC_EXPORT uint64_t gc_get_mixed_gc_count(void);
NOMALLOC_EXPORT uint64_t gc_get_full_gc_count(void);

NOMALLOC_EXPORT int gc_set_watermark(double high, double low);
NOMALLOC_EXPORT void gc_get_watermark(double* high, double* low);

NOMALLOC_EXPORT int gc_set_region_size(size_t size);
NOMALLOC_EXPORT size_t gc_get_region_size(void);

NOMALLOC_EXPORT int gc_get_allocator_stats(struct nomalloc_gc_stats* stats);
NOMALLOC_EXPORT int gc_reset_stats(void);
NOMALLOC_EXPORT int gc_print_stats(void);

NOMALLOC_EXPORT int gc_set_generation_threshold(uint8_t generation, size_t threshold);
NOMALLOC_EXPORT int gc_set_mixed_gc_ratio(double ratio);

NOMALLOC_EXPORT uint64_t gc_api_get_young_gc_count(void);
NOMALLOC_EXPORT uint64_t gc_api_get_mixed_gc_count(void);
NOMALLOC_EXPORT uint64_t gc_api_get_full_gc_count(void);

NOMALLOC_EXPORT const char* gc_state_name(int state);
NOMALLOC_EXPORT const char* gc_phase_name(int phase);

#define GC_STATE_IDLE     0
#define GC_STATE_MARKING  1
#define GC_STATE_COMPACTING 2
#define GC_STATE_CLEANUP  3

#define GC_PHASE_YOUNG    0
#define GC_PHASE_MIXED    1
#define GC_PHASE_FULL     2

#ifdef __cplusplus
}
#endif

#endif