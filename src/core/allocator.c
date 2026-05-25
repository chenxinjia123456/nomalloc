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
#include <sys/mman.h>
#include <unistd.h>

struct allocator g_allocator;
static bool g_allocator_initializing = false;

#define ALLOC_HEADER_SIZE (sizeof(struct alloc_header))
#define ALLOC_HEADER_ALIGNMENT 16

struct alloc_header {
    void* raw_ptr;
    size_t size;
    size_t requested_size;
    uint32_t magic;
    uint32_t flags;
};

#define ALLOC_MAGIC 0x4E4F4D41

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
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_alignment = alignment > page_size ? alignment : page_size;
    size_t total_size = size + aligned_alignment;
    
    void* ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }
    
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned_addr = align_up(addr, alignment);
    
    if (aligned_addr != addr) {
        size_t head_pad = aligned_addr - addr;
        munmap(ptr, head_pad);
    }
    
    size_t tail_pad = total_size - (aligned_addr - addr) - size;
    if (tail_pad > 0) {
        munmap((void*)(aligned_addr + size), tail_pad);
    }
    
    return (void*)aligned_addr;
}

static void system_aligned_free(void* ptr, size_t size) {
    if (!ptr) return;
    munmap(ptr, size);
}

static inline void* add_header(void* raw_ptr, size_t size, size_t requested_size) {
    if (!raw_ptr) return NULL;
    
    struct alloc_header* header = (struct alloc_header*)raw_ptr;
    header->raw_ptr = raw_ptr;
    header->size = size;
    header->requested_size = requested_size;
    header->magic = ALLOC_MAGIC;
    header->flags = 0;
    
    void* user_ptr = (void*)((uintptr_t)raw_ptr + ALLOC_HEADER_SIZE);
    return user_ptr;
}

static inline struct alloc_header* get_header(void* user_ptr) {
    if (!user_ptr) return NULL;
    
    uintptr_t user_addr = (uintptr_t)user_ptr;
    
    if (user_addr < ALLOC_HEADER_SIZE) {
        return NULL;
    }
    
    uintptr_t header_addr = user_addr - ALLOC_HEADER_SIZE;
    
    struct alloc_header* header = (struct alloc_header*)header_addr;
    
    if (header->magic != ALLOC_MAGIC) {
        return NULL;
    }
    
    return header;
}

static inline void* remove_header(void* user_ptr) {
    struct alloc_header* header = get_header(user_ptr);
    if (!header) return user_ptr;
    
    uintptr_t header_addr = (uintptr_t)header;
    return (void*)header_addr;
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
    
    size_t aligned_size = align_up(size, ALLOC_HEADER_ALIGNMENT);
    size_t total_size = aligned_size + ALLOC_HEADER_SIZE + ALLOC_HEADER_ALIGNMENT;
    
    extern void* __libc_malloc(size_t);
    void* raw_ptr = __libc_malloc(total_size);
    if (!raw_ptr) {
        return NULL;
    }
    
    void* user_ptr = add_header(raw_ptr, total_size, size);
    
    atomic64_add_fetch(&g_allocator.total_allocated, size);
    
    return user_ptr;
}

void allocator_free(void* ptr) {
    if (!ptr) return;
    
    extern void __libc_free(void*);
    
    if (!g_allocator.initialized || g_allocator_initializing) {
        __libc_free(ptr);
        return;
    }
    
    struct alloc_header* header = get_header(ptr);
    if (header) {
        size_t size = header->requested_size;
        atomic64_add_fetch(&g_allocator.total_freed, size);
        
        __libc_free(header->raw_ptr);
        return;
    }
    
    __libc_free(ptr);
}

void* allocator_calloc(size_t nmemb, size_t size) {
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            extern void* __libc_calloc(size_t, size_t);
            return __libc_calloc(nmemb, size);
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    size_t total_elements = nmemb * size;
    if (total_elements == 0) {
        total_elements = 1;
    }
    
    void* ptr = allocator_malloc(total_elements);
    if (ptr) {
        memset(ptr, 0, total_elements);
    }
    
    return ptr;
}

void* allocator_realloc(void* ptr, size_t size) {
    extern void* __libc_realloc(void*, size_t);
    extern void* __libc_malloc(size_t);
    extern void __libc_free(void*);
    
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            return __libc_realloc(ptr, size);
        }
        if (allocator_init() != 0) {
            return NULL;
        }
    }
    
    if (!ptr) {
        return allocator_malloc(size);
    }
    
    if (size == 0) {
        allocator_free(ptr);
        return NULL;
    }
    
    struct alloc_header* header = get_header(ptr);
    if (!header) {
        return __libc_realloc(ptr, size);
    }
    
    size_t old_requested_size = header->requested_size;
    
    if (size <= old_requested_size) {
        header->requested_size = size;
        return ptr;
    }
    
    size_t aligned_size = align_up(size, ALLOC_HEADER_ALIGNMENT);
    size_t new_total_size = aligned_size + ALLOC_HEADER_SIZE + ALLOC_HEADER_ALIGNMENT;
    
    void* new_raw_ptr = __libc_malloc(new_total_size);
    if (!new_raw_ptr) {
        return NULL;
    }
    
    memcpy((void*)((uintptr_t)new_raw_ptr + ALLOC_HEADER_SIZE), ptr, old_requested_size);
    
    struct alloc_header* new_header = (struct alloc_header*)new_raw_ptr;
    new_header->raw_ptr = new_raw_ptr;
    new_header->size = new_total_size;
    new_header->requested_size = size;
    new_header->magic = ALLOC_MAGIC;
    new_header->flags = 0;
    
    void* user_ptr = (void*)((uintptr_t)new_raw_ptr + ALLOC_HEADER_SIZE);
    
    atomic64_add_fetch(&g_allocator.total_freed, old_requested_size);
    atomic64_add_fetch(&g_allocator.total_allocated, size);
    
    __libc_free(header->raw_ptr);
    
    return user_ptr;
}

void* allocator_aligned_alloc(size_t alignment, size_t size) {
    if (!g_allocator.initialized) {
        if (g_allocator_initializing) {
            extern void* __libc_malloc(size_t);
            
            size_t alloc_size = size + alignment + ALLOC_HEADER_SIZE;
            void* raw_ptr = __libc_malloc(alloc_size);
            if (!raw_ptr) return NULL;
            
            uintptr_t raw_addr = (uintptr_t)raw_ptr;
            uintptr_t user_addr_target = align_up(raw_addr + ALLOC_HEADER_SIZE, alignment);
            uintptr_t header_addr = user_addr_target - ALLOC_HEADER_SIZE;
            
            struct alloc_header* header = (struct alloc_header*)header_addr;
            header->raw_ptr = raw_ptr;
            header->size = alloc_size;
            header->requested_size = size;
            header->magic = ALLOC_MAGIC;
            header->flags = alignment;
            
            return (void*)user_addr_target;
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
    
    extern void* __libc_malloc(size_t);
    
    size_t alloc_size = size + alignment + ALLOC_HEADER_SIZE;
    void* raw_ptr = __libc_malloc(alloc_size);
    if (!raw_ptr) return NULL;
    
    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t user_addr_target = align_up(raw_addr + ALLOC_HEADER_SIZE, alignment);
    uintptr_t header_addr = user_addr_target - ALLOC_HEADER_SIZE;
    
    struct alloc_header* header = (struct alloc_header*)header_addr;
    header->raw_ptr = raw_ptr;
    header->size = alloc_size;
    header->requested_size = size;
    header->magic = ALLOC_MAGIC;
    header->flags = alignment;
    
    return (void*)user_addr_target;
}

size_t allocator_malloc_usable_size(void* ptr) {
    if (!ptr) return 0;
    
    struct alloc_header* header = get_header(ptr);
    if (!header) {
        return 0;
    }
    
    return header->requested_size;
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