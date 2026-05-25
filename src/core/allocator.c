#define _GNU_SOURCE
#include "allocator.h"
#include "../core/size_class.h"
#include "../memory/arena.h"
#include "../memory/chunk.h"
#include "../cache/tcache.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include "../os/arch/prefetch.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <malloc.h>

struct allocator g_allocator;
static bool g_allocator_initializing = false;

static void* system_malloc(size_t size) {
    extern void* __libc_malloc(size_t);
    return __libc_malloc(size);
}

static void system_free(void* ptr) {
    extern void __libc_free(void*);
    __libc_free(ptr);
}

static void* system_realloc(void* ptr, size_t size) {
    extern void* __libc_realloc(void*, size_t);
    return __libc_realloc(ptr, size);
}

static void* system_aligned_alloc(size_t alignment, size_t size) {
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
}

int allocator_init(void) {
    if (g_allocator.initialized) {
        return 0;
    }
    
    if (g_allocator_initializing) {
        return -1;
    }
    
    g_allocator_initializing = true;
    
    memset(&g_allocator, 0, sizeof(g_allocator));
    
    g_allocator.config.region_size = NOMALLOC_REGION_SIZE_DEFAULT;
    g_allocator.config.tcache_max_size = NOMALLOC_TCACHE_SIZE_DEFAULT;
    g_allocator.config.gc_threshold = NOMALLOC_GC_THRESHOLD_DEFAULT;
    g_allocator.config.gc_watermark_high = NOMALLOC_GC_WATERMARK_HIGH_DEFAULT;
    g_allocator.config.gc_watermark_low = NOMALLOC_GC_WATERMARK_LOW_DEFAULT;
    g_allocator.config.numa_aware = true;
    g_allocator.config.huge_pages = false;
    g_allocator.config.log_level = LOG_LEVEL_INFO;
    g_allocator.config.leak_detection_enabled = false;
    g_allocator.config.stats_enabled = true;
    g_allocator.config.profiler_enabled = false;
    g_allocator.config.profiler_sample_rate = 0.01;
    
    log_init(g_allocator.config.log_level, stderr);
    
    g_allocator.initialized = true;
    
    if (arena_manager_init() != 0) {
        g_allocator_initializing = false;
        g_allocator.initialized = false;
        return -1;
    }
    
    struct arena* arena = arena_create(0);
    if (!arena) {
        arena_manager_shutdown();
        g_allocator_initializing = false;
        g_allocator.initialized = false;
        return -1;
    }
    
    mutex_init(&g_allocator.lock);
    
    if (pthread_key_create(&g_allocator.tcache_key, NULL) != 0) {
        arena_manager_shutdown();
        mutex_destroy(&g_allocator.lock);
        g_allocator_initializing = false;
        g_allocator.initialized = false;
        return -1;
    }
    
    atomic64_init(&g_allocator.total_allocated, 0);
    atomic64_init(&g_allocator.total_freed, 0);
    
    g_allocator_initializing = false;
    
    return 0;
}

void allocator_shutdown(void) {
    g_allocator_initializing = true;
    
    if (!g_allocator.initialized) {
        g_allocator_initializing = false;
        return;
    }
    
    mutex_lock(&g_allocator.lock);
    
    arena_manager_shutdown();
    
    for (size_t i = 0; i < MAX_THREADS; i++) {
        if (g_allocator.tcaches[i]) {
            tcache_destroy(g_allocator.tcaches[i]);
            g_allocator.tcaches[i] = NULL;
        }
    }
    
    pthread_key_delete(g_allocator.tcache_key);
    
    atomic64_store(&g_allocator.total_allocated, 0);
    atomic64_store(&g_allocator.total_freed, 0);
    
    g_allocator.initialized = false;
    
    mutex_unlock(&g_allocator.lock);
    mutex_destroy(&g_allocator.lock);
    
    memset(&g_allocator, 0, sizeof(g_allocator));
    
    log_shutdown();
    
    g_allocator_initializing = false;
}

struct tcache* allocator_get_tcache(void) {
    struct tcache* tcache = (struct tcache*)pthread_getspecific(g_allocator.tcache_key);
    
    if (!tcache) {
        tcache = tcache_create();
        if (!tcache) {
            log_error("Failed to create tcache for thread");
            return NULL;
        }
        
        pthread_setspecific(g_allocator.tcache_key, tcache);
        
        static atomic32_t tcache_id_counter;
        int id = atomic32_inc_fetch(&tcache_id_counter);
        if (id < MAX_THREADS) {
            g_allocator.tcaches[id] = tcache;
        }
        
        log_debug("Created tcache for thread %lu", pthread_self());
    }
    
    return tcache;
}

void* allocator_malloc(size_t size) {
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            return system_malloc(size);
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    if (size == 0) {
        size = 1;
    }
    
    extern void* __libc_malloc(size_t);
    return __libc_malloc(size);
}

void allocator_free(void* ptr) {
    if (!ptr) return;
    
    extern void __libc_free(void*);
    
    if (!g_allocator.initialized || g_allocator_initializing) {
        __libc_free(ptr);
        return;
    }
    
    void** stored_ptr = (void**)((uintptr_t)ptr - sizeof(void*));
    if ((uintptr_t)stored_ptr > 0x10000 && 
        (uintptr_t)*stored_ptr > 0x10000 && 
        (uintptr_t)*stored_ptr <= (uintptr_t)ptr &&
        (uintptr_t)ptr - (uintptr_t)*stored_ptr < 4096) {
        __libc_free(*stored_ptr);
        return;
    }
    
    struct chunk* chunk = ptr_to_chunk(ptr);
    
    __libc_free(ptr);
    return;
}

void* allocator_calloc(size_t nmemb, size_t size) {
    extern void* __libc_calloc(size_t, size_t);
    
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            return __libc_calloc(nmemb, size);
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    return __libc_calloc(nmemb, size);
}

void* allocator_realloc(void* ptr, size_t size) {
    extern void* __libc_realloc(void*, size_t);
    
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            return __libc_realloc(ptr, size);
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    return __libc_realloc(ptr, size);
}

void* allocator_aligned_alloc(size_t alignment, size_t size) {
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            void* ptr = NULL;
            posix_memalign(&ptr, alignment, size);
            return ptr;
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    if (!is_power_of_two(alignment) || alignment == 0) {
        return NULL;
    }
    
    if (size == 0) {
        size = 1;
    }
    
    size_t aligned_size = align_up(size, alignment);
    
    extern void* __libc_malloc(size_t);
    if (alignment <= 16) {
        return __libc_malloc(aligned_size);
    }
    
    size_t alloc_size = aligned_size + alignment;
    void* raw_ptr = __libc_malloc(alloc_size);
    if (!raw_ptr) {
        return NULL;
    }
    
    uintptr_t addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = align_up(addr, alignment);
    
    return (void*)aligned_addr;
}

int allocator_configure(const struct nomalloc_allocator_config* config) {
    if (!config) {
        return -1;
    }
    
    mutex_lock(&g_allocator.lock);
    
    if (config->region_size >= NOMALLOC_REGION_SIZE_MIN && 
        config->region_size <= NOMALLOC_REGION_SIZE_MAX) {
        g_allocator.config.region_size = config->region_size;
    }
    
    if (config->tcache_max_size >= NOMALLOC_TCACHE_SIZE_MIN) {
        g_allocator.config.tcache_max_size = config->tcache_max_size;
    }
    
    if (config->gc_threshold > 0) {
        g_allocator.config.gc_threshold = config->gc_threshold;
    }
    
    if (config->gc_watermark_high > config->gc_watermark_low) {
        g_allocator.config.gc_watermark_high = config->gc_watermark_high;
        g_allocator.config.gc_watermark_low = config->gc_watermark_low;
    }
    
    g_allocator.config.numa_aware = config->numa_aware;
    g_allocator.config.huge_pages = config->huge_pages;
    
    if (config->log_level >= LOG_LEVEL_NONE && config->log_level <= LOG_LEVEL_TRACE) {
        g_allocator.config.log_level = config->log_level;
        log_set_level(config->log_level);
    }
    
    g_allocator.config.leak_detection_enabled = config->leak_detection_enabled;
    g_allocator.config.stats_enabled = config->stats_enabled;
    g_allocator.config.profiler_enabled = config->profiler_enabled;
    
    if (config->profiler_sample_rate >= 0.0 && config->profiler_sample_rate <= 1.0) {
        g_allocator.config.profiler_sample_rate = config->profiler_sample_rate;
    }
    
    mutex_unlock(&g_allocator.lock);
    
    log_info("Allocator configuration updated");
    return 0;
}

void allocator_print_stats(void) {
    printf("\nAllocator Statistics:\n");
    printf("  Initialized: %s\n", g_allocator.initialized ? "yes" : "no");
    printf("  Total allocated: %llu bytes\n", allocator_get_total_allocated());
    printf("  Total freed: %llu bytes\n", allocator_get_total_freed());
    printf("  Active allocations: %llu bytes\n", allocator_get_active_allocations());
    printf("  Region size: %zu bytes\n", g_allocator.config.region_size);
    printf("  Tcache max size: %zu bytes\n", g_allocator.config.tcache_max_size);
    printf("  GC threshold: %zu bytes\n", g_allocator.config.gc_threshold);
    printf("  NUMA aware: %s\n", g_allocator.config.numa_aware ? "yes" : "no");
    printf("  Huge pages: %s\n", g_allocator.config.huge_pages ? "yes" : "no");
    printf("  Log level: %d\n", g_allocator.config.log_level);
    printf("\n");
    
    arena_manager_print_stats();
}