#ifndef NOMALLOC_GC_GC_H
#define NOMALLOC_GC_GC_H

#include "gc_types.h"
#include "gc_region.h"
#include "../utils/atomic.h"
#include "../utils/mutex.h"
#include "../utils/spinlock.h"
#include "../utils/list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gc {
    bool initialized;
    
    struct gc_config config;
    
    struct gc_stats stats;
    
    atomic8_t active;
    atomic8_t concurrent_active;
    
    gc_state_t state;
    gc_phase_t phase;
    
    struct gc_region_manager* region_manager;
    
    struct gc_marking_context marking_ctx;
    
    struct gc_satb_buffer** satb_buffers;
    size_t num_satb_buffers;
    
    uint64_t current_epoch;
    uint64_t threshold_bytes;
    
    size_t young_gen_capacity;
    size_t old_gen_capacity;
    size_t survivor_capacity;
    
    double watermark_high;
    double watermark_low;
    
    size_t pause_target_ms;
    
    mutex_t gc_lock;
    spinlock_t marking_lock;
    
    cond_t gc_cond;
    
    void* gc_thread;
};

extern struct gc g_gc;

int gc_init(void);
void gc_shutdown(void);

int gc_collect(void);
int gc_collect_async(void);
int gc_collect_wait(void);

int gc_collect_young(void);
int gc_collect_mixed(void);
int gc_collect_full(void);

int gc_enable(void);
int gc_disable(void);
bool gc_is_enabled(void);

int gc_set_threshold(size_t threshold);
size_t gc_get_threshold(void);

int gc_set_watermark(double high, double low);
void gc_get_watermark(double* high, double* low);

int gc_set_region_size(size_t size);
size_t gc_get_region_size(void);

int gc_set_pause_target(uint64_t target_ms);
uint64_t gc_get_pause_target(void);

int gc_set_generation_threshold(uint8_t generation, size_t threshold);
int gc_set_mixed_gc_ratio(double ratio);

int gc_get_stats(struct gc_stats* stats);
int gc_reset_stats(void);
int gc_print_stats(void);

const char* gc_state_name(int state);
const char* gc_phase_name(int phase);

static inline bool gc_is_initialized(void) {
    return g_gc.initialized;
}

static inline bool gc_is_active(void) {
    return atomic8_load(&g_gc.active);
}

static inline bool gc_is_concurrent_active(void) {
    return atomic8_load(&g_gc.concurrent_active);
}

static inline gc_state_t gc_get_state(void) {
    return g_gc.state;
}

static inline gc_phase_t gc_get_phase(void) {
    return g_gc.phase;
}

static inline uint64_t gc_get_epoch(void) {
    return g_gc.current_epoch;
}

static inline uint64_t gc_get_count(void) {
    return atomic64_load(&g_gc.stats.gc_count);
}

static inline uint64_t gc_get_young_gc_count(void) {
    return atomic64_load(&g_gc.stats.young_gc_count);
}

static inline uint64_t gc_get_mixed_gc_count(void) {
    return atomic64_load(&g_gc.stats.mixed_gc_count);
}

static inline uint64_t gc_get_full_gc_count(void) {
    return atomic64_load(&g_gc.stats.full_gc_count);
}

static inline uint64_t gc_get_bytes_freed(void) {
    return atomic64_load(&g_gc.stats.bytes_freed);
}

static inline uint64_t gc_get_total_gc_time(void) {
    return atomic64_load(&g_gc.stats.gc_time_ns);
}

static inline double gc_get_avg_gc_time_ms(void) {
    uint64_t count = gc_get_count();
    uint64_t total_time = gc_get_total_gc_time();
    if (count == 0) return 0.0;
    return (double)total_time / (double)count / 1000000.0;
}

static inline size_t gc_get_young_gen_size(void) {
    return atomic64_load(&g_gc.stats.young_gen_size);
}

static inline size_t gc_get_old_gen_size(void) {
    return atomic64_load(&g_gc.stats.old_gen_size);
}

static inline size_t gc_get_survivor_size(void) {
    return atomic64_load(&g_gc.stats.survivor_size);
}

void gc_write_barrier(void* ptr);
void gc_write_barrier_pre(void* old_value, void* new_value);

void gc_register_object(void* ptr, size_t size);
void gc_unregister_object(void* ptr);

bool gc_should_collect(void);
void gc_check_threshold(void);

#ifdef __cplusplus
}
#endif

#endif