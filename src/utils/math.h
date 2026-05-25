#ifndef NOMALLOC_UTILS_MATH_H
#define NOMALLOC_UTILS_MATH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline bool is_power_of_two(size_t val) {
    return val != 0 && (val & (val - 1)) == 0;
}

static inline size_t next_power_of_two(size_t val) {
    if (val == 0) return 1;
    if (is_power_of_two(val)) return val;
    
    size_t result = 1;
    while (result < val) {
        result <<= 1;
    }
    return result;
}

static inline size_t prev_power_of_two(size_t val) {
    if (val == 0) return 0;
    
    size_t result = val;
    while ((result & (result - 1)) != 0) {
        result &= (result - 1);
    }
    return result;
}

static inline size_t round_up_pow2(size_t val, size_t pow2) {
    if (pow2 == 0) return val;
    return (val + pow2 - 1) & ~(pow2 - 1);
}

static inline size_t round_down_pow2(size_t val, size_t pow2) {
    if (pow2 == 0) return val;
    return val & ~(pow2 - 1);
}

static inline size_t align_up(size_t val, size_t alignment) {
    if (alignment == 0) return val;
    return (val + alignment - 1) / alignment * alignment;
}

static inline size_t align_down(size_t val, size_t alignment) {
    if (alignment == 0) return val;
    return val / alignment * alignment;
}

static inline size_t min(size_t a, size_t b) {
    return a < b ? a : b;
}

static inline size_t max(size_t a, size_t b) {
    return a > b ? a : b;
}

static inline int32_t min32(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static inline int32_t max32(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static inline uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static inline uint32_t max_u32(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

static inline uint64_t min_u64(uint64_t a, uint64_t b) {
    return a < b ? a : b;
}

static inline uint64_t max_u64(uint64_t a, uint64_t b) {
    return a > b ? a : b;
}

static inline int clamp(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static inline size_t clamp_size(size_t val, size_t min_val, size_t max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static inline uint32_t log2_u32(uint32_t val) {
    if (val == 0) return 0;
    
    uint32_t result = 0;
    while (val >>= 1) {
        result++;
    }
    return result;
}

static inline uint32_t log2_u64(uint64_t val) {
    if (val == 0) return 0;
    
    uint32_t result = 0;
    while (val >>= 1) {
        result++;
    }
    return result;
}

static inline uint32_t ilog2(uint32_t val) {
    return log2_u32(val);
}

static inline uint32_t ceil_log2(uint32_t val) {
    if (val <= 1) return 0;
    return log2_u32(val - 1) + 1;
}

static inline size_t div_round_up(size_t dividend, size_t divisor) {
    return (dividend + divisor - 1) / divisor;
}

static inline size_t div_round_down(size_t dividend, size_t divisor) {
    return dividend / divisor;
}

static inline uint32_t popcount_u32(uint32_t val) {
    uint32_t count = 0;
    while (val) {
        count += val & 1;
        val >>= 1;
    }
    return count;
}

static inline uint64_t popcount_u64(uint64_t val) {
    uint64_t count = 0;
    while (val) {
        count += val & 1;
        val >>= 1;
    }
    return count;
}

static inline uint32_t count_trailing_zeros_u32(uint32_t val) {
    if (val == 0) return 32;
    
    uint32_t count = 0;
    while ((val & 1) == 0) {
        count++;
        val >>= 1;
    }
    return count;
}

static inline uint32_t count_leading_zeros_u32(uint32_t val) {
    if (val == 0) return 32;
    
    uint32_t count = 0;
    uint32_t mask = 1U << 31;
    while ((val & mask) == 0) {
        count++;
        mask >>= 1;
    }
    return count;
}

static inline uint32_t count_trailing_zeros_u64(uint64_t val) {
    if (val == 0) return 64;
    
    uint32_t count = 0;
    while ((val & 1) == 0) {
        count++;
        val >>= 1;
    }
    return count;
}

static inline uint32_t count_leading_zeros_u64(uint64_t val) {
    if (val == 0) return 64;
    
    uint32_t count = 0;
    uint64_t mask = 1ULL << 63;
    while ((val & mask) == 0) {
        count++;
        mask >>= 1;
    }
    return count;
}

#ifdef __cplusplus
}
#endif

#endif