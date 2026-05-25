#ifndef NOMALLOC_UTILS_ATOMIC_H
#define NOMALLOC_UTILS_ATOMIC_H

#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef atomic_uint_fast64_t atomic64_t;
typedef atomic_uint_fast32_t atomic32_t;
typedef atomic_uint_fast8_t atomic8_t;
typedef atomic_bool atomic_bool_t;
typedef _Atomic(void*) atomic_ptr_t;

static inline void atomic64_init(atomic64_t* var, uint64_t value) {
    atomic_init(var, value);
}

static inline uint64_t atomic64_load(const atomic64_t* var) {
    return atomic_load(var);
}

static inline void atomic64_store(atomic64_t* var, uint64_t value) {
    atomic_store(var, value);
}

static inline uint64_t atomic64_add_fetch(atomic64_t* var, uint64_t value) {
    return atomic_fetch_add(var, value) + value;
}

static inline uint64_t atomic64_sub_fetch(atomic64_t* var, uint64_t value) {
    return atomic_fetch_sub(var, value) - value;
}

static inline uint64_t atomic64_inc_fetch(atomic64_t* var) {
    return atomic_fetch_add(var, 1) + 1;
}

static inline uint64_t atomic64_dec_fetch(atomic64_t* var) {
    return atomic_fetch_sub(var, 1) - 1;
}

static inline void atomic64_inc(atomic64_t* var) {
    atomic_fetch_add(var, 1);
}

static inline void atomic64_dec(atomic64_t* var) {
    atomic_fetch_sub(var, 1);
}

static inline bool atomic64_compare_exchange(atomic64_t* var, 
                                              uint64_t* expected, 
                                              uint64_t desired) {
    return atomic_compare_exchange_strong(var, expected, desired);
}

static inline bool atomic64_compare_exchange_weak(atomic64_t* var, 
                                                   uint64_t* expected, 
                                                   uint64_t desired) {
    return atomic_compare_exchange_weak(var, expected, desired);
}

static inline uint64_t atomic64_exchange(atomic64_t* var, uint64_t desired) {
    return atomic_exchange(var, desired);
}

static inline void atomic32_init(atomic32_t* var, uint32_t value) {
    atomic_init(var, value);
}

static inline uint32_t atomic32_load(const atomic32_t* var) {
    return atomic_load(var);
}

static inline void atomic32_store(atomic32_t* var, uint32_t value) {
    atomic_store(var, value);
}

static inline uint32_t atomic32_add_fetch(atomic32_t* var, uint32_t value) {
    return atomic_fetch_add(var, value) + value;
}

static inline uint32_t atomic32_sub_fetch(atomic32_t* var, uint32_t value) {
    return atomic_fetch_sub(var, value) - value;
}

static inline uint32_t atomic32_inc_fetch(atomic32_t* var) {
    return atomic_fetch_add(var, 1) + 1;
}

static inline uint32_t atomic32_dec_fetch(atomic32_t* var) {
    return atomic_fetch_sub(var, 1) - 1;
}

static inline void atomic32_inc(atomic32_t* var) {
    atomic_fetch_add(var, 1);
}

static inline void atomic32_dec(atomic32_t* var) {
    atomic_fetch_sub(var, 1);
}

static inline bool atomic32_compare_exchange(atomic32_t* var, 
                                              atomic32_t* expected, 
                                              uint32_t desired) {
    return atomic_compare_exchange_strong(var, expected, desired);
}

static inline uint32_t atomic32_exchange(atomic32_t* var, uint32_t desired) {
    return atomic_exchange(var, desired);
}

static inline void atomic8_init(atomic8_t* var, uint8_t value) {
    atomic_init(var, value);
}

static inline uint8_t atomic8_load(const atomic8_t* var) {
    return atomic_load(var);
}

static inline void atomic8_store(atomic8_t* var, uint8_t value) {
    atomic_store(var, value);
}

static inline bool atomic8_compare_exchange(atomic8_t* var, 
                                             uint8_t* expected, 
                                             uint8_t desired) {
    return atomic_compare_exchange_strong(var, expected, desired);
}

static inline void atomic_fence(void) {
    atomic_thread_fence(memory_order_seq_cst);
}

static inline void atomic_fence_acquire(void) {
    atomic_thread_fence(memory_order_acquire);
}

static inline void atomic_fence_release(void) {
    atomic_thread_fence(memory_order_release);
}

static inline void atomic_fence_relaxed(void) {
    atomic_thread_fence(memory_order_relaxed);
}

#ifdef __cplusplus
}
#endif

#endif