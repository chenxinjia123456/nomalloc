#ifndef NOMALLOC_OS_HUGEPAGES_H
#define NOMALLOC_OS_HUGEPAGES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../utils/atomic.h"
#include "../utils/spinlock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HUGEPAGE_2MB_SIZE   (2 * 1024 * 1024)
#define HUGEPAGE_1GB_SIZE   (1024 * 1024 * 1024)
#define HUGEPAGE_DEFAULT_SIZE HUGEPAGE_2MB_SIZE

#define HUGEPAGE_MODE_NONE     0
#define HUGEPAGE_MODE_EXPLICIT 1
#define HUGEPAGE_MODE_TRANSPARENT 2
#define HUGEPAGE_MODE_BOTH     3

struct hugepage_stats {
    atomic64_t total_pages;
    atomic64_t free_pages;
    atomic64_t reserved_pages;
    atomic64_t surplus_pages;
    
    atomic64_t allocations_2mb;
    atomic64_t allocations_1gb;
    atomic64_t allocations_failed;
    
    atomic64_t total_allocated_bytes;
    atomic64_t total_freed_bytes;
    
    atomic64_t transparent_enabled;
    atomic64_t transparent_always;
};

struct hugepage_config {
    bool enabled;
    int mode;
    
    size_t page_size;
    size_t min_alloc_size;
    size_t max_alloc_size;
    
    size_t reserve_pages;
    size_t min_free_pages;
    
    bool fallback_to_regular;
    bool transparent_enabled;
    bool transparent_always;
    bool transparent_madvise;
};

struct hugepage_pool {
    void** pages_2mb;
    size_t num_pages_2mb;
    size_t capacity_2mb;
    
    void** pages_1gb;
    size_t num_pages_1gb;
    size_t capacity_1gb;
    
    spinlock_t lock;
    
    struct hugepage_stats stats;
};

struct hugepage_manager {
    bool initialized;
    bool available;
    
    struct hugepage_config config;
    struct hugepage_pool pool;
    
    spinlock_t global_lock;
};

extern struct hugepage_manager g_hugepage_manager;

int hugepage_init(void);
void hugepage_shutdown(void);

bool hugepage_is_available(void);
bool hugepage_is_enabled(void);

int hugepage_configure(const struct hugepage_config* config);
int hugepage_get_config(struct hugepage_config* config);

size_t hugepage_get_size(void);
size_t hugepage_get_2mb_size(void);
size_t hugepage_get_1gb_size(void);

size_t hugepage_get_total_pages(void);
size_t hugepage_get_free_pages(void);
size_t hugepage_get_reserved_pages(void);

void* hugepage_alloc(size_t size);
void* hugepage_alloc_2mb(size_t num_pages);
void* hugepage_alloc_1gb(size_t num_pages);
void hugepage_free(void* ptr, size_t size);

int hugepage_reserve(size_t num_pages);
int hugepage_release_reserved(size_t num_pages);

bool hugepage_should_use(size_t size);
size_t hugepage_align_size(size_t size);

void hugepage_enable_transparent(void);
void hugepage_disable_transparent(void);
bool hugepage_is_transparent_enabled(void);

void* hugepage_alloc_transparent(size_t size);
void hugepage_free_transparent(void* ptr, size_t size);

void hugepage_print_stats(void);
int hugepage_get_stats(struct hugepage_stats* stats);

static inline bool hugepage_is_size_aligned(size_t size, size_t page_size) {
    return (size % page_size) == 0;
}

static inline size_t hugepage_round_up(size_t size, size_t page_size) {
    return ((size + page_size - 1) / page_size) * page_size;
}

static inline size_t hugepage_num_pages_needed(size_t size, size_t page_size) {
    return hugepage_round_up(size, page_size) / page_size;
}

static inline bool hugepage_is_2mb_aligned(void* ptr) {
    return ((uintptr_t)ptr % HUGEPAGE_2MB_SIZE) == 0;
}

static inline bool hugepage_is_1gb_aligned(void* ptr) {
    return ((uintptr_t)ptr % HUGEPAGE_1GB_SIZE) == 0;
}

static inline size_t hugepage_get_allocated_bytes(void) {
    return atomic64_load(&g_hugepage_manager.pool.stats.total_allocated_bytes);
}

static inline size_t hugepage_get_freed_bytes(void) {
    return atomic64_load(&g_hugepage_manager.pool.stats.total_freed_bytes);
}

static inline size_t hugepage_get_active_bytes(void) {
    return hugepage_get_allocated_bytes() - hugepage_get_freed_bytes();
}

#ifdef __cplusplus
}
#endif

#endif