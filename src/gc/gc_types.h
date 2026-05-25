#ifndef NOMALLOC_GC_GC_TYPES_H
#define NOMALLOC_GC_GC_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../utils/bitmap.h"
#include <stdatomic.h>
#include "../utils/bitmap.h"
#include "../utils/list.h"
#include "../utils/spinlock.h"
#include "../utils/atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GC_REGION_SIZE_DEFAULT (2 * 1024 * 1024)
#define GC_REGION_SIZE_MIN     (256 * 1024)
#define GC_REGION_SIZE_MAX     (32 * 1024 * 1024)

#define GC_MAX_REGIONS 1024
#define GC_MAX_ARENA_REGIONS 256

#define GC_YOUNG_GEN_RATIO 0.3
#define GC_SURVIVOR_RATIO 0.1
#define GC_OLD_GEN_RATIO 0.6

#define GC_PAUSE_TARGET_MS 100
#define GC_PAUSE_TARGET_MIN_MS 10
#define GC_PAUSE_TARGET_MAX_MS 500

#define GC_MARK_BITMAP_BITS_PER_BLOCK 1

typedef enum {
    GC_REGION_EMPTY = 0,
    GC_REGION_YOUNG,
    GC_REGION_SURVIVOR,
    GC_REGION_OLD,
    GC_REGION_HUMONGOUS,
    GC_REGION_PINNED
} gc_region_type_t;

typedef enum {
    GC_PHASE_NONE = 0,
    GC_PHASE_MARK_START,
    GC_PHASE_MARK_CONCURRENT,
    GC_PHASE_MARK_FINAL,
    GC_PHASE_MIXED_RECLAIM_START,
    GC_PHASE_MIXED_RECLAIM,
    GC_PHASE_CLEANUP,
    GC_PHASE_IDLE
} gc_phase_t;

typedef enum {
    GC_STATE_IDLE = 0,
    GC_STATE_YOUNG_GC,
    GC_STATE_MIXED_GC,
    GC_STATE_FULL_GC,
    GC_STATE_MARKING,
    GC_STATE_COMPACTING
} gc_state_t;

typedef enum {
    GC_COLOR_WHITE = 0,
    GC_COLOR_GRAY,
    GC_COLOR_BLACK
} gc_color_t;

struct gc_region {
    int id;
    int arena_id;
    
    gc_region_type_t type;
    gc_region_type_t prev_type;
    
    void* base_addr;
    size_t size;
    
    size_t allocated_bytes;
    size_t live_bytes;
    size_t garbage_bytes;
    
    bitmap_t marking_bitmap;
    bitmap_t live_bitmap;
    size_t bitmap_size;
    
    uint32_t age;
    uint32_t gc_epoch;
    
    bool needs_reclaim;
    bool is_compacting_target;
    bool is_relocation_target;
    
    size_t relocation_dest_id;
    
    spinlock_t lock;
    
    struct list_head list;
    struct list_head reclaim_list;
    struct list_head compact_list;
};

struct gc_remembered_set {
    struct gc_region** regions;
    size_t num_regions;
    size_t capacity;
    
    bitmap_t card_bitmap;
    size_t card_bitmap_size;
    
    spinlock_t lock;
};

struct gc_marking_context {
    struct gc_region** gray_regions;
    size_t gray_count;
    size_t gray_capacity;
    
    void** mark_stack;
    size_t mark_stack_top;
    size_t mark_stack_capacity;
    
    atomic8_t marking_active;
    atomic8_t marking_complete;
    
    uint64_t marked_objects;
    uint64_t marked_bytes;
};

struct gc_satb_buffer {
    void** buffer;
    size_t capacity;
    size_t count;
    
    atomic8_t active;
    
    spinlock_t lock;
};

struct gc_stats {
    atomic64_t gc_count;
    atomic64_t young_gc_count;
    atomic64_t mixed_gc_count;
    atomic64_t full_gc_count;
    
    atomic64_t gc_time_ns;
    atomic64_t pause_time_ns;
    
    atomic64_t bytes_freed;
    atomic64_t regions_reclaimed;
    
    atomic64_t young_gen_size;
    atomic64_t old_gen_size;
    atomic64_t survivor_size;
    
    atomic64_t fragmentation_bytes;
    atomic64_t compaction_count;
    
    atomic64_t mark_time_ns;
    atomic64_t reclaim_time_ns;
    atomic64_t compact_time_ns;
};

struct gc_config {
    size_t region_size;
    size_t max_regions;
    
    double young_gen_ratio;
    double survivor_ratio;
    double old_gen_ratio;
    
    uint64_t pause_target_ms;
    uint64_t pause_target_min_ms;
    uint64_t pause_target_max_ms;
    
    bool concurrent_marking;
    bool parallel_reclaim;
    bool mixed_gc_enabled;
    
    size_t marking_threads;
    size_t reclaim_threads;
    
    double gc_watermark_high;
    double gc_watermark_low;
    
    size_t gc_threshold;
};

#ifdef __cplusplus
}
#endif

#endif