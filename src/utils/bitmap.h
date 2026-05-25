#ifndef NOMALLOC_UTILS_BITMAP_H
#define NOMALLOC_UTILS_BITMAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BITMAP_WORD_SIZE 64
#define BITMAP_WORD_SHIFT 6
#define BITMAP_WORD_MASK 63

typedef uint64_t bitmap_word_t;
typedef bitmap_word_t* bitmap_t;

static inline size_t bitmap_size(size_t num_bits) {
    return (num_bits + BITMAP_WORD_SIZE - 1) >> BITMAP_WORD_SHIFT;
}

static inline size_t bitmap_alloc_size(size_t num_bits) {
    return bitmap_size(num_bits) * sizeof(bitmap_word_t);
}

static inline void bitmap_init(bitmap_t bitmap, size_t num_bits, bool value) {
    size_t num_words = bitmap_size(num_bits);
    bitmap_word_t fill = value ? ~0ULL : 0ULL;
    memset(bitmap, fill, num_words * sizeof(bitmap_word_t));
}

static inline void bitmap_clear(bitmap_t bitmap, size_t num_bits) {
    bitmap_init(bitmap, num_bits, false);
}

static inline void bitmap_fill(bitmap_t bitmap, size_t num_bits) {
    bitmap_init(bitmap, num_bits, true);
}

static inline bool bitmap_get(const bitmap_t bitmap, size_t index) {
    size_t word_idx = index >> BITMAP_WORD_SHIFT;
    size_t bit_idx = index & BITMAP_WORD_MASK;
    return (bitmap[word_idx] >> bit_idx) & 1ULL;
}

static inline void bitmap_set(bitmap_t bitmap, size_t index) {
    size_t word_idx = index >> BITMAP_WORD_SHIFT;
    size_t bit_idx = index & BITMAP_WORD_MASK;
    bitmap[word_idx] |= (1ULL << bit_idx);
}

static inline void bitmap_clear_bit(bitmap_t bitmap, size_t index) {
    size_t word_idx = index >> BITMAP_WORD_SHIFT;
    size_t bit_idx = index & BITMAP_WORD_MASK;
    bitmap[word_idx] &= ~(1ULL << bit_idx);
}

static inline void bitmap_set_bit(bitmap_t bitmap, size_t index, bool value) {
    if (value) {
        bitmap_set(bitmap, index);
    } else {
        bitmap_clear_bit(bitmap, index);
    }
}

static inline bool bitmap_test_and_set(bitmap_t bitmap, size_t index) {
    size_t word_idx = index >> BITMAP_WORD_SHIFT;
    size_t bit_idx = index & BITMAP_WORD_MASK;
    bitmap_word_t mask = 1ULL << bit_idx;
    bitmap_word_t old = bitmap[word_idx];
    bitmap[word_idx] |= mask;
    return (old & mask) != 0;
}

static inline bool bitmap_test_and_clear(bitmap_t bitmap, size_t index) {
    size_t word_idx = index >> BITMAP_WORD_SHIFT;
    size_t bit_idx = index & BITMAP_WORD_MASK;
    bitmap_word_t mask = 1ULL << bit_idx;
    bitmap_word_t old = bitmap[word_idx];
    bitmap[word_idx] &= ~mask;
    return (old & mask) != 0;
}

static inline size_t bitmap_find_first_set(const bitmap_t bitmap, size_t num_bits) {
    size_t num_words = bitmap_size(num_bits);
    for (size_t i = 0; i < num_words; i++) {
        if (bitmap[i] != 0) {
            for (size_t j = 0; j < BITMAP_WORD_SIZE; j++) {
                if ((bitmap[i] >> j) & 1ULL) {
                    size_t index = (i << BITMAP_WORD_SHIFT) + j;
                    if (index < num_bits) {
                        return index;
                    }
                }
            }
        }
    }
    return num_bits;
}

static inline size_t bitmap_find_first_clear(const bitmap_t bitmap, size_t num_bits) {
    size_t num_words = bitmap_size(num_bits);
    for (size_t i = 0; i < num_words; i++) {
        if (bitmap[i] != ~0ULL) {
            for (size_t j = 0; j < BITMAP_WORD_SIZE; j++) {
                if (!((bitmap[i] >> j) & 1ULL)) {
                    size_t index = (i << BITMAP_WORD_SHIFT) + j;
                    if (index < num_bits) {
                        return index;
                    }
                }
            }
        }
    }
    return num_bits;
}

static inline size_t bitmap_count_set(const bitmap_t bitmap, size_t num_bits) {
    size_t count = 0;
    size_t num_words = bitmap_size(num_bits);
    for (size_t i = 0; i < num_words; i++) {
        bitmap_word_t word = bitmap[i];
        while (word) {
            count += word & 1ULL;
            word >>= 1;
        }
    }
    return count;
}

static inline size_t bitmap_count_clear(const bitmap_t bitmap, size_t num_bits) {
    return num_bits - bitmap_count_set(bitmap, num_bits);
}

static inline void bitmap_or(bitmap_t dst, const bitmap_t src1, 
                              const bitmap_t src2, size_t num_bits) {
    size_t num_words = bitmap_size(num_bits);
    for (size_t i = 0; i < num_words; i++) {
        dst[i] = src1[i] | src2[i];
    }
}

static inline void bitmap_and(bitmap_t dst, const bitmap_t src1, 
                               const bitmap_t src2, size_t num_bits) {
    size_t num_words = bitmap_size(num_bits);
    for (size_t i = 0; i < num_words; i++) {
        dst[i] = src1[i] & src2[i];
    }
}

static inline void bitmap_xor(bitmap_t dst, const bitmap_t src1, 
                               const bitmap_t src2, size_t num_bits) {
    size_t num_words = bitmap_size(num_bits);
    for (size_t i = 0; i < num_words; i++) {
        dst[i] = src1[i] ^ src2[i];
    }
}

static inline void bitmap_not(bitmap_t dst, const bitmap_t src, size_t num_bits) {
    size_t num_words = bitmap_size(num_bits);
    for (size_t i = 0; i < num_words; i++) {
        dst[i] = ~src[i];
    }
}

static inline bool bitmap_equal(const bitmap_t bitmap1, const bitmap_t bitmap2, 
                                 size_t num_bits) {
    size_t num_words = bitmap_size(num_bits);
    return memcmp(bitmap1, bitmap2, num_words * sizeof(bitmap_word_t)) == 0;
}

static inline bool bitmap_is_empty(const bitmap_t bitmap, size_t num_bits) {
    size_t num_words = bitmap_size(num_bits);
    for (size_t i = 0; i < num_words; i++) {
        if (bitmap[i] != 0) {
            return false;
        }
    }
    return true;
}

static inline bool bitmap_is_full(const bitmap_t bitmap, size_t num_bits) {
    size_t num_words = bitmap_size(num_bits);
    for (size_t i = 0; i < num_words; i++) {
        if (bitmap[i] != ~0ULL) {
            return false;
        }
    }
    return true;
}

#ifdef __cplusplus
}
#endif

#endif