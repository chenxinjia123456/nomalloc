#ifndef NOMALLOC_ARCH_BARRIER_H
#define NOMALLOC_ARCH_BARRIER_H

#include <stdint.h>
#include "../os/arch/x86_64.h"
#include "../os/arch/arm64.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__x86_64__) || defined(_M_X64)

#define barrier_acquire() x86_64_lfence()
#define barrier_release() x86_64_sfence()
#define barrier_full()     x86_64_mfence()

#elif defined(__aarch64__) || defined(_M_ARM64)

#define barrier_acquire() arm64_dmb_ld()
#define barrier_release() arm64_dmb_st()
#define barrier_full()     arm64_dmb()

#else

#define barrier_acquire() ((void)0)
#define barrier_release() ((void)0)
#define barrier_full()     ((void)0)

#endif

#define barrier_compiler() __asm__ volatile("" ::: "memory")

#define barrier_acquire_compiler() do { barrier_compiler(); barrier_acquire(); } while (0)
#define barrier_release_compiler() do { barrier_release(); barrier_compiler(); } while (0)
#define barrier_full_compiler()    do { barrier_full(); barrier_compiler(); } while (0)

#define READ_ONCE(x) (*(const volatile typeof(x)*)&(x))
#define WRITE_ONCE(x, v) (*(volatile typeof(x)*)&(x) = (v))

#define smp_load_acquire(p) ({ \
    typeof(*p) ___p1 = READ_ONCE(*p); \
    barrier_acquire_compiler(); \
    ___p1; \
})

#define smp_store_release(p, v) do { \
    barrier_release_compiler(); \
    WRITE_ONCE(*p, v); \
} while (0)

#define smp_mb()  barrier_full_compiler()
#define smp_rmb() barrier_acquire_compiler()
#define smp_wmb() barrier_release_compiler()

#define lock_barrier() smp_mb()
#define unlock_barrier() smp_mb()

static inline void memory_fence_before_atomic(void) {
    barrier_release_compiler();
}

static inline void memory_fence_after_atomic(void) {
    barrier_acquire_compiler();
}

static inline void memory_fence_full(void) {
    barrier_full_compiler();
}

#ifdef __cplusplus
}
#endif

#endif