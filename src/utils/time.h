#ifndef NOMALLOC_UTILS_TIME_H
#define NOMALLOC_UTILS_TIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static inline uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static inline double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static inline uint64_t time_get_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline uint64_t time_get_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static inline uint64_t time_get_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static inline uint64_t time_get_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec;
}

static inline uint64_t time_get_real_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline uint64_t time_get_cpu_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline uint64_t time_get_thread_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

typedef struct {
    uint64_t start_ns;
    uint64_t end_ns;
    bool running;
} nomalloc_timer_t;

static inline void timer_start(nomalloc_timer_t* timer) {
    timer->start_ns = time_get_ns();
    timer->running = true;
}

static inline void timer_stop(nomalloc_timer_t* timer) {
    timer->end_ns = time_get_ns();
    timer->running = false;
}

static inline uint64_t timer_elapsed_ns(nomalloc_timer_t* timer) {
    if (timer->running) {
        return time_get_ns() - timer->start_ns;
    }
    return timer->end_ns - timer->start_ns;
}

static inline uint64_t timer_elapsed_us(nomalloc_timer_t* timer) {
    return timer_elapsed_ns(timer) / 1000ULL;
}

static inline uint64_t timer_elapsed_ms(nomalloc_timer_t* timer) {
    return timer_elapsed_ns(timer) / 1000000ULL;
}

static inline double timer_elapsed_sec(nomalloc_timer_t* timer) {
    return (double)timer_elapsed_ns(timer) / 1000000000.0;
}

static inline void timer_reset(nomalloc_timer_t* timer) {
    timer->start_ns = 0;
    timer->end_ns = 0;
    timer->running = false;
}

static inline bool timer_is_running(nomalloc_timer_t* timer) {
    return timer->running;
}

typedef struct {
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t total_ns;
    uint64_t count;
    uint64_t* samples;
    size_t sample_capacity;
    size_t sample_count;
} latency_stats_t;

static inline void latency_stats_init(latency_stats_t* stats, size_t sample_capacity) {
    stats->min_ns = UINT64_MAX;
    stats->max_ns = 0;
    stats->total_ns = 0;
    stats->count = 0;
    stats->samples = (uint64_t*)malloc(sample_capacity * sizeof(uint64_t));
    stats->sample_capacity = sample_capacity;
    stats->sample_count = 0;
}

static inline void latency_stats_destroy(latency_stats_t* stats) {
    free(stats->samples);
    stats->samples = NULL;
}

static inline void latency_stats_record(latency_stats_t* stats, uint64_t latency_ns) {
    if (latency_ns < stats->min_ns) {
        stats->min_ns = latency_ns;
    }
    if (latency_ns > stats->max_ns) {
        stats->max_ns = latency_ns;
    }
    stats->total_ns += latency_ns;
    stats->count++;
    
    if (stats->sample_count < stats->sample_capacity) {
        stats->samples[stats->sample_count++] = latency_ns;
    }
}

static inline uint64_t latency_stats_get_min(latency_stats_t* stats) {
    return stats->min_ns;
}

static inline uint64_t latency_stats_get_max(latency_stats_t* stats) {
    return stats->max_ns;
}

static inline uint64_t latency_stats_get_avg(latency_stats_t* stats) {
    if (stats->count == 0) return 0;
    return stats->total_ns / stats->count;
}

static inline uint64_t latency_stats_get_p50(latency_stats_t* stats) {
    if (stats->sample_count == 0) return 0;
    
    size_t mid = stats->sample_count / 2;
    uint64_t* sorted = (uint64_t*)malloc(stats->sample_count * sizeof(uint64_t));
    memcpy(sorted, stats->samples, stats->sample_count * sizeof(uint64_t));
    
    for (size_t i = 0; i < stats->sample_count - 1; i++) {
        for (size_t j = i + 1; j < stats->sample_count; j++) {
            if (sorted[i] > sorted[j]) {
                uint64_t tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
    
    uint64_t result = sorted[mid];
    free(sorted);
    return result;
}

static inline uint64_t latency_stats_get_p99(latency_stats_t* stats) {
    if (stats->sample_count == 0) return 0;
    
    size_t idx = (size_t)(stats->sample_count * 0.99);
    if (idx >= stats->sample_count) idx = stats->sample_count - 1;
    
    uint64_t* sorted = (uint64_t*)malloc(stats->sample_count * sizeof(uint64_t));
    memcpy(sorted, stats->samples, stats->sample_count * sizeof(uint64_t));
    
    for (size_t i = 0; i < stats->sample_count - 1; i++) {
        for (size_t j = i + 1; j < stats->sample_count; j++) {
            if (sorted[i] > sorted[j]) {
                uint64_t tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
    
    uint64_t result = sorted[idx];
    free(sorted);
    return result;
}

static inline void latency_stats_reset(latency_stats_t* stats) {
    stats->min_ns = UINT64_MAX;
    stats->max_ns = 0;
    stats->total_ns = 0;
    stats->count = 0;
    stats->sample_count = 0;
}

#ifdef __cplusplus
}
#endif

#endif