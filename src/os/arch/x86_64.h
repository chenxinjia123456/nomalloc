#ifndef NOMALLOC_ARCH_X86_64_H
#define NOMALLOC_ARCH_X86_64_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__x86_64__) || defined(_M_X64)

static inline void x86_64_pause(void) {
    __asm__ volatile("pause" ::: "memory");
}

static inline void x86_64_mfence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static inline void x86_64_lfence(void) {
    __asm__ volatile("lfence" ::: "memory");
}

static inline void x86_64_sfence(void) {
    __asm__ volatile("sfence" ::: "memory");
}

static inline void x86_64_prefetch_t0(const void* ptr) {
    __asm__ volatile("prefetcht0 %0" : : "m"(*(const char*)ptr) : "memory");
}

static inline void x86_64_prefetch_t1(const void* ptr) {
    __asm__ volatile("prefetcht1 %0" : : "m"(*(const char*)ptr) : "memory");
}

static inline void x86_64_prefetch_t2(const void* ptr) {
    __asm__ volatile("prefetcht2 %0" : : "m"(*(const char*)ptr) : "memory");
}

static inline void x86_64_prefetch_nta(const void* ptr) {
    __asm__ volatile("prefetchnta %0" : : "m"(*(const char*)ptr) : "memory");
}

static inline void x86_64_clflush(const void* ptr) {
    __asm__ volatile("clflush %0" : : "m"(*(const char*)ptr) : "memory");
}

static inline void x86_64_clwb(const void* ptr) {
    __asm__ volatile("clwb %0" : : "m"(*(const char*)ptr) : "memory");
}

static inline void x86_64_clflushopt(const void* ptr) {
    __asm__ volatile("clflushopt %0" : : "m"(*(const char*)ptr) : "memory");
}

static inline uint64_t x86_64_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t x86_64_rdtscp(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi) :: "rcx");
    return ((uint64_t)hi << 32) | lo;
}

static inline void x86_64_serialize(void) {
    __asm__ volatile("cpuid" ::: "eax", "ebx", "ecx", "edx", "memory");
}

static inline uint32_t x86_64_popcnt(uint32_t val) {
    uint32_t result;
    __asm__ volatile("popcntl %1, %0" : "=r"(result) : "r"(val));
    return result;
}

static inline uint64_t x86_64_popcnt64(uint64_t val) {
    uint64_t result;
    __asm__ volatile("popcntq %1, %0" : "=r"(result) : "r"(val));
    return result;
}

static inline uint32_t x86_64_lzcnt(uint32_t val) {
    uint32_t result;
    __asm__ volatile("lzcntl %1, %0" : "=r"(result) : "r"(val));
    return result;
}

static inline uint64_t x86_64_lzcnt64(uint64_t val) {
    uint64_t result;
    __asm__ volatile("lzcntq %1, %0" : "=r"(result) : "r"(val));
    return result;
}

static inline uint32_t x86_64_count_trailing_zeros(uint32_t val) {
    uint32_t result;
    __asm__ volatile("tzcntl %1, %0" : "=r"(result) : "r"(val));
    return result;
}

static inline uint64_t x86_64_count_trailing_zeros64(uint64_t val) {
    uint64_t result;
    __asm__ volatile("tzcntq %1, %0" : "=r"(result) : "r"(val));
    return result;
}

#define x86_64_prefetch_l1(ptr) x86_64_prefetch_t0(ptr)
#define x86_64_prefetch_l2(ptr) x86_64_prefetch_t1(ptr)
#define x86_64_prefetch_l3(ptr) x86_64_prefetch_t2(ptr)

#define x86_64_cache_line_size 64

#else

#define x86_64_pause() ((void)0)
#define x86_64_mfence() ((void)0)
#define x86_64_lfence() ((void)0)
#define x86_64_sfence() ((void)0)
#define x86_64_prefetch_t0(ptr) ((void)0)
#define x86_64_prefetch_t1(ptr) ((void)0)
#define x86_64_prefetch_t2(ptr) ((void)0)
#define x86_64_prefetch_nta(ptr) ((void)0)
#define x86_64_clflush(ptr) ((void)0)
#define x86_64_clwb(ptr) ((void)0)
#define x86_64_clflushopt(ptr) ((void)0)
#define x86_64_rdtsc() (0)
#define x86_64_rdtscp() (0)
#define x86_64_serialize() ((void)0)
#define x86_64_cache_line_size 64

#endif

#ifdef __cplusplus
}
#endif

#endif