#ifndef NOMALLOC_ARCH_ARM64_H
#define NOMALLOC_ARCH_ARM64_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__aarch64__) || defined(_M_ARM64)

static inline void arm64_yield(void) {
    __asm__ volatile("yield" ::: "memory");
}

static inline void arm64_dmb(void) {
    __asm__ volatile("dmb ish" ::: "memory");
}

static inline void arm64_dsb(void) {
    __asm__ volatile("dsb ish" ::: "memory");
}

static inline void arm64_isb(void) {
    __asm__ volatile("isb" ::: "memory");
}

static inline void arm64_dmb_ld(void) {
    __asm__ volatile("dmb ishld" ::: "memory");
}

static inline void arm64_dmb_st(void) {
    __asm__ volatile("dmb ishst" ::: "memory");
}

static inline void arm64_prefetch_l1_keep(const void* ptr) {
    __asm__ volatile("prfm pldl1keep, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_prefetch_l1_strm(const void* ptr) {
    __asm__ volatile("prfm pldl1strm, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_prefetch_l2_keep(const void* ptr) {
    __asm__ volatile("prfm pldl2keep, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_prefetch_l2_strm(const void* ptr) {
    __asm__ volatile("prfm pldl2strm, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_prefetch_l3_keep(const void* ptr) {
    __asm__ volatile("prfm pldl3keep, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_prefetch_l3_strm(const void* ptr) {
    __asm__ volatile("prfm pldl3strm, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_prefetch_inst(const void* ptr) {
    __asm__ volatile("prfm plil1keep, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_dc_zva(void* ptr) {
    __asm__ volatile("dc zva, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_dc_civac(void* ptr) {
    __asm__ volatile("dc civac, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_dc_cvac(void* ptr) {
    __asm__ volatile("dc cvac, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_dc_cvau(void* ptr) {
    __asm__ volatile("dc cvau, %0" : : "r"(ptr) : "memory");
}

static inline void arm64_ic_ivau(void* ptr) {
    __asm__ volatile("ic ivau, %0" : : "r"(ptr) : "memory");
    arm64_dsb();
    arm64_isb();
}

static inline uint64_t arm64_cntvct_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

static inline uint64_t arm64_cntfrq_el0(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static inline uint64_t arm64_get_cycle_count(void) {
    return arm64_cntvct_el0();
}

static inline uint32_t arm64_clz(uint32_t val) {
    uint32_t result;
    __asm__ volatile("clz %w0, %w1" : "=r"(result) : "r"(val));
    return result;
}

static inline uint64_t arm64_clz64(uint64_t val) {
    uint64_t result;
    __asm__ volatile("clz %0, %1" : "=r"(result) : "r"(val));
    return result;
}

static inline uint32_t arm64_rbit(uint32_t val) {
    uint32_t result;
    __asm__ volatile("rbit %w0, %w1" : "=r"(result) : "r"(val));
    return result;
}

static inline uint64_t arm64_rbit64(uint64_t val) {
    uint64_t result;
    __asm__ volatile("rbit %0, %1" : "=r"(result) : "r"(val));
    return result;
}

#define arm64_prefetch_l1(ptr) arm64_prefetch_l1_keep(ptr)
#define arm64_prefetch_l2(ptr) arm64_prefetch_l2_keep(ptr)
#define arm64_prefetch_l3(ptr) arm64_prefetch_l3_keep(ptr)

#define arm64_cache_line_size 64

#else

#define arm64_yield() ((void)0)
#define arm64_dmb() ((void)0)
#define arm64_dsb() ((void)0)
#define arm64_isb() ((void)0)
#define arm64_dmb_ld() ((void)0)
#define arm64_dmb_st() ((void)0)
#define arm64_prefetch_l1_keep(ptr) ((void)0)
#define arm64_prefetch_l1_strm(ptr) ((void)0)
#define arm64_prefetch_l2_keep(ptr) ((void)0)
#define arm64_prefetch_l2_strm(ptr) ((void)0)
#define arm64_prefetch_l3_keep(ptr) ((void)0)
#define arm64_prefetch_l3_strm(ptr) ((void)0)
#define arm64_prefetch_inst(ptr) ((void)0)
#define arm64_dc_zva(ptr) ((void)0)
#define arm64_dc_civac(ptr) ((void)0)
#define arm64_dc_cvac(ptr) ((void)0)
#define arm64_dc_cvau(ptr) ((void)0)
#define arm64_ic_ivau(ptr) ((void)0)
#define arm64_cntvct_el0() (0)
#define arm64_cntfrq_el0() (0)
#define arm64_get_cycle_count() (0)
#define arm64_cache_line_size 64

#endif

#ifdef __cplusplus
}
#endif

#endif