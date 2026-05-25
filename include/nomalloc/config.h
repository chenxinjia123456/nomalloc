#ifndef NOMALLOC_CONFIG_H
#define NOMALLOC_CONFIG_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

int nomalloc_init(void);
void nomalloc_shutdown(void);

int nomalloc_configure(const struct nomalloc_allocator_config* config);

int nomalloc_set_region_size(size_t size);
size_t nomalloc_get_region_size(void);

int nomalloc_set_tcache_size(size_t size);
size_t nomalloc_get_tcache_size(void);

int nomalloc_set_gc_threshold(size_t threshold);
size_t nomalloc_get_gc_threshold(void);

int nomalloc_set_gc_watermark(double high, double low);
void nomalloc_get_gc_watermark(double* high, double* low);

int nomalloc_enable_numa(bool enable);
bool nomalloc_is_numa_enabled(void);

int nomalloc_enable_huge_pages(bool enable);
bool nomalloc_is_huge_pages_enabled(void);

int nomalloc_set_log_level(int level);
int nomalloc_get_log_level(void);

int nomalloc_enable_leak_detection(bool enable);
bool nomalloc_is_leak_detection_enabled(void);

int nomalloc_enable_stats(bool enable);
bool nomalloc_is_stats_enabled(void);

int nomalloc_enable_profiler(bool enable, double sample_rate);
bool nomalloc_is_profiler_enabled(void);
double nomalloc_get_profiler_sample_rate(void);

int nomalloc_load_config_from_file(const char* config_file);
int nomalloc_load_config_from_env(void);

struct nomalloc_allocator_config* nomalloc_get_default_config(void);
void nomalloc_print_config(void);

#ifdef __cplusplus
}
#endif

#endif