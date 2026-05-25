#ifndef NOMALLOC_UTILS_ASSERT_H
#define NOMALLOC_UTILS_ASSERT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NDEBUG

#define nomalloc_assert(...) ((void)0)
#define nomalloc_assert_msg(...) ((void)0)
#define nomalloc_assert_not_null(ptr) ((void)0)
#define nomalloc_assert_in_range(val, min, max) ((void)0)
#define nomalloc_assert_aligned(ptr, alignment) ((void)0)
#define nomalloc_assert_power_of_two(val) ((void)0)

#else

#define nomalloc_assert(...) do { \
    if (!(__VA_ARGS__)) { \
        log_error("Assertion failed"); \
        fprintf(stderr, "Assertion failed at %s:%d in %s\n", \
                __FILE__, __LINE__, __func__); \
        abort(); \
    } \
} while (0)

#define nomalloc_assert_msg(cond, msg) do { \
    if (!(cond)) { \
        log_error("Assertion failed: %s - %s", #cond, msg); \
        fprintf(stderr, "Assertion failed: %s - %s at %s:%d in %s\n", \
                #cond, msg, __FILE__, __LINE__, __func__); \
        abort(); \
    } \
} while (0)

#define nomalloc_assert_not_null(ptr) do { \
    if ((ptr) == NULL) { \
        log_error("Null pointer: %s", #ptr); \
        fprintf(stderr, "Null pointer: %s at %s:%d in %s\n", \
                #ptr, __FILE__, __LINE__, __func__); \
        abort(); \
    } \
} while (0)

#define nomalloc_assert_in_range(val, min, max) do { \
    if ((val) < (min) || (val) > (max)) { \
        log_error("Value out of range: %s = %llu (expected [%llu, %llu])", \
                  #val, (unsigned long long)(val), \
                  (unsigned long long)(min), (unsigned long long)(max)); \
        fprintf(stderr, "Value out of range: %s at %s:%d in %s\n", \
                #val, __FILE__, __LINE__, __func__); \
        abort(); \
    } \
} while (0)

#define nomalloc_assert_aligned(ptr, alignment) do { \
    if (((uintptr_t)(ptr) & ((alignment) - 1)) != 0) { \
        log_error("Pointer not aligned: %s = %p (expected alignment %llu)", \
                  #ptr, (ptr), (unsigned long long)(alignment)); \
        fprintf(stderr, "Pointer not aligned: %s at %s:%d in %s\n", \
                #ptr, __FILE__, __LINE__, __func__); \
        abort(); \
    } \
} while (0)

#define nomalloc_assert_power_of_two(val) do { \
    if ((val) == 0 || ((val) & ((val) - 1)) != 0) { \
        log_error("Value not power of two: %s = %llu", \
                  #val, (unsigned long long)(val)); \
        fprintf(stderr, "Value not power of two: %s at %s:%d in %s\n", \
                #val, __FILE__, __LINE__, __func__); \
        abort(); \
    } \
} while (0)

#endif

#define nomalloc_static_assert(cond, msg) _Static_assert(cond, msg)

#define nomalloc_unreachable() do { \
    log_error("Unreachable code reached"); \
    fprintf(stderr, "Unreachable code reached at %s:%d in %s\n", \
            __FILE__, __LINE__, __func__); \
    abort(); \
} while (0)

#define nomalloc_panic(msg) do { \
    log_error("Panic: %s", msg); \
    fprintf(stderr, "Panic: %s at %s:%d in %s\n", \
            msg, __FILE__, __LINE__, __func__); \
    abort(); \
} while (0)

#ifdef __cplusplus
}
#endif

#endif