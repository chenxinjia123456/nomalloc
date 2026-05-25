#ifndef NOMALLOC_H
#define NOMALLOC_H

#include "types.h"
#include "config.h"
#include "posix_api.h"
#include "gc_api.h"
#include "stats_api.h"
#include "debug_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOMALLOC_EXPORT __attribute__((visibility("default")))

NOMALLOC_EXPORT int nomalloc_init(void);
NOMALLOC_EXPORT void nomalloc_shutdown(void);

NOMALLOC_EXPORT int nomalloc_startup(void);
NOMALLOC_EXPORT void nomalloc_cleanup(void);

NOMALLOC_EXPORT const char* nomalloc_version(void);
NOMALLOC_EXPORT const char* nomalloc_build_info(void);

NOMALLOC_EXPORT size_t nomalloc_get_total_allocated(void);
NOMALLOC_EXPORT size_t nomalloc_get_total_freed(void);
NOMALLOC_EXPORT size_t nomalloc_get_active_allocations(void);

NOMALLOC_EXPORT double nomalloc_get_fragmentation_ratio(void);
NOMALLOC_EXPORT double nomalloc_get_memory_utilization(void);

NOMALLOC_EXPORT int nomalloc_print_stats_summary(void);
NOMALLOC_EXPORT int nomalloc_export_stats_json(const char* filename);
NOMALLOC_EXPORT int nomalloc_export_stats_csv(const char* filename);

#ifdef __cplusplus
}
#endif

#endif