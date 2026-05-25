#ifndef NOMALLOC_DEBUG_LEAK_DETECTOR_H
#define NOMALLOC_DEBUG_LEAK_DETECTOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../utils/atomic.h"
#include "../utils/spinlock.h"
#include "../utils/list.h"
#include "../utils/hash.h"
#include <nomalloc/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LEAK_DETECTOR_MAX_ALLOCATIONS 100000
#define LEAK_DETECTOR_HASH_SIZE 8192
#define LEAK_DETECTOR_STACK_DEPTH 16
#define LEAK_DETECTOR_REPORT_SIZE 64

struct allocation_record {
    void* ptr;
    size_t size;
    size_t class_idx;
    int arena_id;
    uint64_t thread_id;
    uint64_t timestamp;
    
    void* stack_trace[LEAK_DETECTOR_STACK_DEPTH];
    int stack_depth;
    
    struct hlist_node hash_node;
    struct list_head list;
};

struct leak_detector_stats {
    atomic64_t total_allocations;
    atomic64_t total_deallocations;
    atomic64_t leaked_allocations;
    atomic64_t leaked_bytes;
    
    atomic64_t peak_allocations;
    atomic64_t peak_bytes;
    
    atomic8_t tracking_active;
};

struct leak_report_entry {
    size_t size;
    uint64_t count;
    uint64_t total_bytes;
    uint64_t first_seen;
    uint64_t last_seen;
    
    void* common_stack_trace[LEAK_DETECTOR_STACK_DEPTH];
    int stack_depth;
};

struct leak_report {
    struct leak_report_entry entries[LEAK_DETECTOR_REPORT_SIZE];
    size_t num_entries;
    
    uint64_t total_leaked_bytes;
    uint64_t total_leaked_objects;
};

struct leak_detector {
    bool enabled;
    bool tracking;
    
    struct allocation_record** records;
    size_t num_records;
    size_t max_records;
    
    struct hlist_head hash_table[LEAK_DETECTOR_HASH_SIZE];
    struct list_head allocation_list;
    struct list_head free_list;
    
    struct leak_detector_stats stats;
    
    spinlock_t lock;
    
    bool verbose;
};

extern struct leak_detector g_leak_detector;

int leak_detector_init(void);
void leak_detector_shutdown(void);

int leak_detector_enable(void);
int leak_detector_disable(void);
bool leak_detector_is_enabled(void);

int leak_detector_start_tracking(void);
int leak_detector_stop_tracking(void);
bool leak_detector_is_tracking(void);

void leak_detector_record_alloc(void* ptr, size_t size, size_t class_idx, int arena_id);
void leak_detector_record_dealloc(void* ptr);
void leak_detector_update_stats(void);

int leak_detector_get_stats(struct nomalloc_leak_stats* stats);
int leak_detector_generate_report(const char* filename);
int leak_detector_print_report(void);
int leak_detector_clear(void);

void leak_detector_dump_summary(void);

static inline uint64_t leak_detector_get_leaked_count(void) {
    return atomic64_load(&g_leak_detector.stats.leaked_allocations);
}

static inline uint64_t leak_detector_get_leaked_bytes(void) {
    return atomic64_load(&g_leak_detector.stats.leaked_bytes);
}

static inline double leak_detector_get_leak_ratio(void) {
    uint64_t total = atomic64_load(&g_leak_detector.stats.total_allocations);
    uint64_t leaked = leak_detector_get_leaked_count();
    
    if (total == 0) return 0.0;
    return (double)leaked / (double)total;
}

#ifdef __cplusplus
}
#endif

#endif