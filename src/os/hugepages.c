#include "hugepages.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

struct hugepage_manager g_hugepage_manager;

static int hugepage_read_sysfs_value(const char* path, size_t* value) {
    FILE* fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    
    char buf[64];
    if (fgets(buf, sizeof(buf), fp) == NULL) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    
    *value = (size_t)atol(buf);
    return 0;
}

static void hugepage_stats_init(struct hugepage_stats* stats) {
    atomic64_init(&stats->total_pages, 0);
    atomic64_init(&stats->free_pages, 0);
    atomic64_init(&stats->reserved_pages, 0);
    atomic64_init(&stats->surplus_pages, 0);
    atomic64_init(&stats->allocations_2mb, 0);
    atomic64_init(&stats->allocations_1gb, 0);
    atomic64_init(&stats->allocations_failed, 0);
    atomic64_init(&stats->total_allocated_bytes, 0);
    atomic64_init(&stats->total_freed_bytes, 0);
    atomic64_init(&stats->transparent_enabled, 0);
    atomic64_init(&stats->transparent_always, 0);
}

static void hugepage_pool_init(struct hugepage_pool* pool) {
    pool->pages_2mb = NULL;
    pool->num_pages_2mb = 0;
    pool->capacity_2mb = 0;
    
    pool->pages_1gb = NULL;
    pool->num_pages_1gb = 0;
    pool->capacity_1gb = 0;
    
    spinlock_init(&pool->lock);
    
    hugepage_stats_init(&pool->stats);
}

int hugepage_init(void) {
    if (g_hugepage_manager.initialized) {
        log_warn("Hugepages already initialized");
        return 0;
    }
    
    memset(&g_hugepage_manager, 0, sizeof(g_hugepage_manager));
    
    spinlock_init(&g_hugepage_manager.global_lock);
    
    g_hugepage_manager.config.enabled = false;
    g_hugepage_manager.config.mode = HUGEPAGE_MODE_TRANSPARENT;
    g_hugepage_manager.config.page_size = HUGEPAGE_DEFAULT_SIZE;
    g_hugepage_manager.config.min_alloc_size = HUGEPAGE_DEFAULT_SIZE;
    g_hugepage_manager.config.max_alloc_size = 1024 * HUGEPAGE_DEFAULT_SIZE;
    g_hugepage_manager.config.reserve_pages = 0;
    g_hugepage_manager.config.min_free_pages = 0;
    g_hugepage_manager.config.fallback_to_regular = true;
    g_hugepage_manager.config.transparent_enabled = true;
    g_hugepage_manager.config.transparent_always = false;
    g_hugepage_manager.config.transparent_madvise = true;
    
    hugepage_pool_init(&g_hugepage_manager.pool);
    
    size_t total_pages = 0;
    size_t free_pages = 0;
    
    if (hugepage_read_sysfs_value("/proc/sys/vm/nr_hugepages", &total_pages) == 0 &&
        hugepage_read_sysfs_value("/proc/sys/vm/nr_hugepages_tlb", &free_pages) == 0) {
        
        g_hugepage_manager.available = true;
        atomic64_store(&g_hugepage_manager.pool.stats.total_pages, total_pages);
        atomic64_store(&g_hugepage_manager.pool.stats.free_pages, free_pages);
        
        log_info("Hugepages available: total=%zu, free=%zu", total_pages, free_pages);
    } else {
        g_hugepage_manager.available = false;
        
        size_t transparent_enabled = 0;
        if (hugepage_read_sysfs_value("/sys/kernel/mm/transparent_hugepage/enabled", 
                                      &transparent_enabled) == 0) {
            g_hugepage_manager.config.transparent_enabled = true;
            atomic64_store(&g_hugepage_manager.pool.stats.transparent_enabled, transparent_enabled);
        }
        
        log_info("Explicit hugepages not available, transparent hugepages: %s",
                 g_hugepage_manager.config.transparent_enabled ? "enabled" : "disabled");
    }
    
    g_hugepage_manager.initialized = true;
    
    return 0;
}

void hugepage_shutdown(void) {
    if (!g_hugepage_manager.initialized) {
        return;
    }
    
    spinlock_lock(&g_hugepage_manager.global_lock);
    
    spinlock_lock(&g_hugepage_manager.pool.lock);
    
    if (g_hugepage_manager.pool.pages_2mb) {
        for (size_t i = 0; i < g_hugepage_manager.pool.num_pages_2mb; i++) {
            if (g_hugepage_manager.pool.pages_2mb[i]) {
                hugepage_free(g_hugepage_manager.pool.pages_2mb[i], HUGEPAGE_2MB_SIZE);
            }
        }
        free(g_hugepage_manager.pool.pages_2mb);
    }
    
    if (g_hugepage_manager.pool.pages_1gb) {
        for (size_t i = 0; i < g_hugepage_manager.pool.num_pages_1gb; i++) {
            if (g_hugepage_manager.pool.pages_1gb[i]) {
                hugepage_free(g_hugepage_manager.pool.pages_1gb[i], HUGEPAGE_1GB_SIZE);
            }
        }
        free(g_hugepage_manager.pool.pages_1gb);
    }
    
    spinlock_unlock(&g_hugepage_manager.pool.lock);
    
    g_hugepage_manager.initialized = false;
    
    spinlock_unlock(&g_hugepage_manager.global_lock);
    
    log_info("Hugepages shutdown: allocated=%zu bytes, freed=%zu bytes",
             hugepage_get_allocated_bytes(), hugepage_get_freed_bytes());
}

bool hugepage_is_available(void) {
    return g_hugepage_manager.available;
}

bool hugepage_is_enabled(void) {
    return g_hugepage_manager.config.enabled;
}

int hugepage_configure(const struct hugepage_config* config) {
    if (!config) return -1;
    
    spinlock_lock(&g_hugepage_manager.global_lock);
    
    g_hugepage_manager.config = *config;
    
    if (config->reserve_pages > 0) {
        hugepage_reserve(config->reserve_pages);
    }
    
    spinlock_unlock(&g_hugepage_manager.global_lock);
    
    log_info("Hugepages configured: mode=%d, page_size=%zu, min_alloc=%zu",
             config->mode, config->page_size, config->min_alloc_size);
    
    return 0;
}

int hugepage_get_config(struct hugepage_config* config) {
    if (!config) return -1;
    
    *config = g_hugepage_manager.config;
    return 0;
}

size_t hugepage_get_size(void) {
    return g_hugepage_manager.config.page_size;
}

size_t hugepage_get_2mb_size(void) {
    return HUGEPAGE_2MB_SIZE;
}

size_t hugepage_get_1gb_size(void) {
    return HUGEPAGE_1GB_SIZE;
}

size_t hugepage_get_total_pages(void) {
    return atomic64_load(&g_hugepage_manager.pool.stats.total_pages);
}

size_t hugepage_get_free_pages(void) {
    return atomic64_load(&g_hugepage_manager.pool.stats.free_pages);
}

size_t hugepage_get_reserved_pages(void) {
    return atomic64_load(&g_hugepage_manager.pool.stats.reserved_pages);
}

void* hugepage_alloc(size_t size) {
    if (!g_hugepage_manager.initialized) {
        return NULL;
    }
    
    size_t aligned_size = hugepage_align_size(size);
    
    if (aligned_size >= HUGEPAGE_1GB_SIZE) {
        size_t num_pages = hugepage_num_pages_needed(size, HUGEPAGE_1GB_SIZE);
        return hugepage_alloc_1gb(num_pages);
    } else {
        size_t num_pages = hugepage_num_pages_needed(size, HUGEPAGE_2MB_SIZE);
        return hugepage_alloc_2mb(num_pages);
    }
}

void* hugepage_alloc_2mb(size_t num_pages) {
    if (!g_hugepage_manager.initialized || num_pages == 0) {
        return NULL;
    }
    
    size_t total_size = num_pages * HUGEPAGE_2MB_SIZE;
    
    void* ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    
    if (ptr == MAP_FAILED) {
        atomic64_inc(&g_hugepage_manager.pool.stats.allocations_failed);
        
        if (g_hugepage_manager.config.fallback_to_regular) {
            ptr = memory_alloc_aligned(total_size, HUGEPAGE_2MB_SIZE);
            
            if (ptr) {
                madvise(ptr, total_size, MADV_HUGEPAGE);
                log_debug("Hugepage allocation failed, using transparent hugepage: ptr=%p, size=%zu",
                          ptr, total_size);
            }
        }
        
        if (ptr == MAP_FAILED || ptr == NULL) {
            log_error("Hugepage allocation failed: num_pages=%zu, size=%zu",
                      num_pages, total_size);
            return NULL;
        }
    } else {
        atomic64_inc(&g_hugepage_manager.pool.stats.allocations_2mb);
        atomic64_add_fetch(&g_hugepage_manager.pool.stats.total_allocated_bytes, total_size);
        
        log_trace("Hugepage 2MB allocated: ptr=%p, num_pages=%zu, size=%zu",
                  ptr, num_pages, total_size);
    }
    
    return ptr;
}

void* hugepage_alloc_1gb(size_t num_pages) {
    if (!g_hugepage_manager.initialized || num_pages == 0) {
        return NULL;
    }
    
    size_t total_size = num_pages * HUGEPAGE_1GB_SIZE;
    
    void* ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_1GB, -1, 0);
    
    if (ptr == MAP_FAILED) {
        atomic64_inc(&g_hugepage_manager.pool.stats.allocations_failed);
        
        if (g_hugepage_manager.config.fallback_to_regular) {
            ptr = memory_alloc_aligned(total_size, HUGEPAGE_1GB_SIZE);
        }
        
        if (ptr == MAP_FAILED || ptr == NULL) {
            log_error("Hugepage 1GB allocation failed: num_pages=%zu, size=%zu",
                      num_pages, total_size);
            return NULL;
        }
    } else {
        atomic64_inc(&g_hugepage_manager.pool.stats.allocations_1gb);
        atomic64_add_fetch(&g_hugepage_manager.pool.stats.total_allocated_bytes, total_size);
    }
    
    return ptr;
}

void hugepage_free(void* ptr, size_t size) {
    if (!ptr) return;
    
    size_t aligned_size = hugepage_align_size(size);
    
    munmap(ptr, aligned_size);
    
    atomic64_add_fetch(&g_hugepage_manager.pool.stats.total_freed_bytes, aligned_size);
    
    log_trace("Hugepage freed: ptr=%p, size=%zu", ptr, aligned_size);
}

int hugepage_reserve(size_t num_pages) {
    if (!g_hugepage_manager.initialized) {
        return -1;
    }
    
    FILE* fp = fopen("/proc/sys/vm/nr_hugepages", "w");
    if (!fp) {
        log_error("Failed to open /proc/sys/vm/nr_hugepages for writing");
        return -1;
    }
    
    fprintf(fp, "%zu", num_pages);
    fclose(fp);
    
    atomic64_add_fetch(&g_hugepage_manager.pool.stats.reserved_pages, num_pages);
    
    log_debug("Hugepages reserved: num_pages=%zu", num_pages);
    
    return 0;
}

int hugepage_release_reserved(size_t num_pages) {
    if (!g_hugepage_manager.initialized) {
        return -1;
    }
    
    size_t current = hugepage_get_reserved_pages();
    if (num_pages > current) {
        num_pages = current;
    }
    
    FILE* fp = fopen("/proc/sys/vm/nr_hugepages", "w");
    if (!fp) {
        return -1;
    }
    
    fprintf(fp, "0");
    fclose(fp);
    
    atomic64_sub_fetch(&g_hugepage_manager.pool.stats.reserved_pages, num_pages);
    
    log_debug("Hugepages released: num_pages=%zu", num_pages);
    
    return 0;
}

bool hugepage_should_use(size_t size) {
    if (!g_hugepage_manager.initialized || !g_hugepage_manager.config.enabled) {
        return false;
    }
    
    return size >= g_hugepage_manager.config.min_alloc_size;
}

size_t hugepage_align_size(size_t size) {
    if (size >= HUGEPAGE_1GB_SIZE) {
        return hugepage_round_up(size, HUGEPAGE_1GB_SIZE);
    } else if (size >= HUGEPAGE_2MB_SIZE) {
        return hugepage_round_up(size, HUGEPAGE_2MB_SIZE);
    } else {
        return size;
    }
}

void hugepage_enable_transparent(void) {
    if (!g_hugepage_manager.initialized) return;
    
    g_hugepage_manager.config.transparent_enabled = true;
    g_hugepage_manager.config.transparent_always = true;
    
    log_debug("Transparent hugepages enabled (always mode)");
}

void hugepage_disable_transparent(void) {
    if (!g_hugepage_manager.initialized) return;
    
    g_hugepage_manager.config.transparent_enabled = false;
    g_hugepage_manager.config.transparent_always = false;
    
    log_debug("Transparent hugepages disabled");
}

bool hugepage_is_transparent_enabled(void) {
    return g_hugepage_manager.config.transparent_enabled;
}

void* hugepage_alloc_transparent(size_t size) {
    if (!g_hugepage_manager.initialized || !g_hugepage_manager.config.transparent_enabled) {
        return memory_alloc_aligned(size, get_page_size());
    }
    
    size_t aligned_size = hugepage_round_up(size, HUGEPAGE_2MB_SIZE);
    
    void* ptr = memory_alloc_aligned(aligned_size, HUGEPAGE_2MB_SIZE);
    
    if (ptr) {
        madvise(ptr, aligned_size, 
                g_hugepage_manager.config.transparent_always ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
    }
    
    return ptr;
}

void hugepage_free_transparent(void* ptr, size_t size) {
    if (!ptr) return;
    
    memory_free_aligned(ptr, hugepage_align_size(size), HUGEPAGE_2MB_SIZE);
}

void hugepage_print_stats(void) {
    printf("\nHugepages Statistics:\n");
    printf("  Available: %s\n", g_hugepage_manager.available ? "yes" : "no");
    printf("  Enabled: %s\n", g_hugepage_manager.config.enabled ? "yes" : "no");
    printf("  Mode: %d\n", g_hugepage_manager.config.mode);
    printf("  Page size: %zu bytes (%.2f MB)\n", 
           g_hugepage_manager.config.page_size, 
           g_hugepage_manager.config.page_size / (1024.0 * 1024.0));
    printf("  Total pages: %zu\n", hugepage_get_total_pages());
    printf("  Free pages: %zu\n", hugepage_get_free_pages());
    printf("  Reserved pages: %zu\n", hugepage_get_reserved_pages());
    printf("  2MB allocations: %llu\n", 
           atomic64_load(&g_hugepage_manager.pool.stats.allocations_2mb));
    printf("  1GB allocations: %llu\n", 
           atomic64_load(&g_hugepage_manager.pool.stats.allocations_1gb));
    printf("  Failed allocations: %llu\n", 
           atomic64_load(&g_hugepage_manager.pool.stats.allocations_failed));
    printf("  Total allocated: %zu bytes (%.2f GB)\n", 
           hugepage_get_allocated_bytes(), 
           hugepage_get_allocated_bytes() / (1024.0 * 1024.0 * 1024.0));
    printf("  Total freed: %zu bytes (%.2f GB)\n", 
           hugepage_get_freed_bytes(),
           hugepage_get_freed_bytes() / (1024.0 * 1024.0 * 1024.0));
    printf("  Active: %zu bytes (%.2f GB)\n", 
           hugepage_get_active_bytes(),
           hugepage_get_active_bytes() / (1024.0 * 1024.0 * 1024.0));
    printf("  Transparent enabled: %s\n", 
           g_hugepage_manager.config.transparent_enabled ? "yes" : "no");
    printf("  Transparent always: %s\n", 
           g_hugepage_manager.config.transparent_always ? "yes" : "no");
    printf("  Fallback to regular: %s\n", 
           g_hugepage_manager.config.fallback_to_regular ? "yes" : "no");
    printf("  Min alloc size: %zu bytes (%.2f MB)\n", 
           g_hugepage_manager.config.min_alloc_size,
           g_hugepage_manager.config.min_alloc_size / (1024.0 * 1024.0));
    printf("  Max alloc size: %zu bytes (%.2f GB)\n", 
           g_hugepage_manager.config.max_alloc_size,
           g_hugepage_manager.config.max_alloc_size / (1024.0 * 1024.0 * 1024.0));
}

int hugepage_get_stats(struct hugepage_stats* stats) {
    if (!stats) return -1;
    
    stats->total_pages = g_hugepage_manager.pool.stats.total_pages;
    stats->free_pages = g_hugepage_manager.pool.stats.free_pages;
    stats->reserved_pages = g_hugepage_manager.pool.stats.reserved_pages;
    stats->surplus_pages = g_hugepage_manager.pool.stats.surplus_pages;
    stats->allocations_2mb = g_hugepage_manager.pool.stats.allocations_2mb;
    stats->allocations_1gb = g_hugepage_manager.pool.stats.allocations_1gb;
    stats->allocations_failed = g_hugepage_manager.pool.stats.allocations_failed;
    stats->total_allocated_bytes = g_hugepage_manager.pool.stats.total_allocated_bytes;
    stats->total_freed_bytes = g_hugepage_manager.pool.stats.total_freed_bytes;
    stats->transparent_enabled = g_hugepage_manager.pool.stats.transparent_enabled;
    stats->transparent_always = g_hugepage_manager.pool.stats.transparent_always;
    
    return 0;
}