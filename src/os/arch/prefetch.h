#ifndef NOMALLOC_ARCH_PREFETCH_H
#define NOMALLOC_ARCH_PREFETCH_H

#include <stdint.h>
#include "x86_64.h"
#include "arm64.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__x86_64__) || defined(_M_X64)

#define prefetch_l1(ptr) x86_64_prefetch_t0(ptr)
#define prefetch_l2(ptr) x86_64_prefetch_t1(ptr)
#define prefetch_l3(ptr) x86_64_prefetch_t2(ptr)
#define prefetch_nta(ptr) x86_64_prefetch_nta(ptr)

#define cache_flush(ptr) x86_64_clflush(ptr)
#define cache_flush_opt(ptr) x86_64_clflushopt(ptr)
#define cache_write_back(ptr) x86_64_clwb(ptr)

#define memory_barrier_full() x86_64_mfence()
#define memory_barrier_load() x86_64_lfence()
#define memory_barrier_store() x86_64_sfence()

#define cpu_pause() x86_64_pause()

#elif defined(__aarch64__) || defined(_M_ARM64)

#define prefetch_l1(ptr) arm64_prefetch_l1_keep(ptr)
#define prefetch_l2(ptr) arm64_prefetch_l2_keep(ptr)
#define prefetch_l3(ptr) arm64_prefetch_l3_keep(ptr)
#define prefetch_nta(ptr) arm64_prefetch_l1_strm(ptr)

#define cache_flush(ptr) arm64_dc_civac(ptr)
#define cache_flush_opt(ptr) arm64_dc_civac(ptr)
#define cache_write_back(ptr) arm64_dc_cvac(ptr)

#define memory_barrier_full() arm64_dmb()
#define memory_barrier_load() arm64_dmb_ld()
#define memory_barrier_store() arm64_dmb_st()

#define cpu_pause() arm64_yield()

#else

#define prefetch_l1(ptr) ((void)0)
#define prefetch_l2(ptr) ((void)0)
#define prefetch_l3(ptr) ((void)0)
#define prefetch_nta(ptr) ((void)0)

#define cache_flush(ptr) ((void)0)
#define cache_flush_opt(ptr) ((void)0)
#define cache_write_back(ptr) ((void)0)

#define memory_barrier_full() ((void)0)
#define memory_barrier_load() ((void)0)
#define memory_barrier_store() ((void)0)

#define cpu_pause() ((void)0)

#endif

static inline void prefetch_range(const void* ptr, size_t size) {
    const char* addr = (const char*)ptr;
    const char* end = addr + size;
    
    size_t cache_line_size = 64;
    
    for (; addr < end; addr += cache_line_size) {
        prefetch_l1(addr);
    }
}

static inline void prefetch_for_read(const void* ptr) {
    prefetch_l1(ptr);
}

static inline void prefetch_for_write(const void* ptr) {
    prefetch_l2(ptr);
}

static inline void prefetch_for_temporal(const void* ptr) {
    prefetch_l3(ptr);
}

static inline void prefetch_for_non_temporal(const void* ptr) {
    prefetch_nta(ptr);
}

#ifdef __cplusplus
}
#endif

#endif