#ifndef NOMALLOC_CORE_SIZE_CLASS_H
#define NOMALLOC_CORE_SIZE_CLASS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <nomalloc/types.h>
#include "../utils/math.h"
#include "../utils/memory.h"
#include "../utils/assert.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NUM_SMALL_CLASSES
#define NUM_SMALL_CLASSES NOMALLOC_NUM_SIZE_CLASSES_SMALL
#endif

#ifndef NUM_MEDIUM_CLASSES
#define NUM_MEDIUM_CLASSES NOMALLOC_NUM_SIZE_CLASSES_MEDIUM
#endif

#ifndef NUM_SIZE_CLASSES
#define NUM_SIZE_CLASSES NOMALLOC_NUM_SIZE_CLASSES_TOTAL
#endif

#define SIZE_CLASS_MIN 8
#define SIZE_CLASS_SMALL_MAX NOMALLOC_SMALL_SIZE_MAX
#define SIZE_CLASS_MEDIUM_MAX NOMALLOC_MEDIUM_SIZE_MAX
#define SIZE_CLASS_LARGE_THRESHOLD SIZE_CLASS_MEDIUM_MAX

static const size_t small_size_classes[NUM_SMALL_CLASSES] = {
    8, 16, 32, 48, 64, 80, 96, 112, 128,
    160, 192, 224, 256, 320, 384, 448, 512,
    640, 768, 896, 1024, 1280, 1536, 1792, 2048,
    2560, 3072, 3584, 4096
};

static const size_t medium_size_classes[NUM_MEDIUM_CLASSES] = {
    8192, 12288, 16384, 20480, 24576, 28672, 32768,
    49152, 65536, 98304, 131072, 196608, 262144,
    393216, 524288, 786432, 1048576
};

static inline size_t size_to_class_small(size_t size) {
    nomalloc_assert(size <= SIZE_CLASS_SMALL_MAX, "size too large for small class");
    
    if (size <= 8) return 0;
    
    for (size_t i = 0; i < NUM_SMALL_CLASSES; i++) {
        if (size <= small_size_classes[i]) {
            return i;
        }
    }
    
    return NUM_SMALL_CLASSES - 1;
}

static inline size_t size_to_class_medium(size_t size) {
    nomalloc_assert(size > SIZE_CLASS_SMALL_MAX && size <= SIZE_CLASS_MEDIUM_MAX, 
                    "size out of medium range");
    
    size_t adjusted_size = size;
    if (adjusted_size <= SIZE_CLASS_SMALL_MAX) {
        adjusted_size = SIZE_CLASS_SMALL_MAX + 1;
    }
    
    for (size_t i = 0; i < NUM_MEDIUM_CLASSES; i++) {
        if (adjusted_size <= medium_size_classes[i]) {
            return NUM_SMALL_CLASSES + i;
        }
    }
    
    return NUM_SIZE_CLASSES - 1;
}

static inline size_t size_to_class(size_t size) {
    nomalloc_assert(size > 0, "size must be positive");
    
    if (size <= SIZE_CLASS_SMALL_MAX) {
        return size_to_class_small(size);
    } else if (size <= SIZE_CLASS_MEDIUM_MAX) {
        return size_to_class_medium(size);
    } else {
        return NUM_SIZE_CLASSES;
    }
}

static inline size_t class_to_size(size_t class_idx) {
    if (class_idx < NUM_SMALL_CLASSES) {
        return small_size_classes[class_idx];
    } else if (class_idx < NUM_SIZE_CLASSES) {
        return medium_size_classes[class_idx - NUM_SMALL_CLASSES];
    } else {
        return 0;
    }
}

static inline bool is_small_class(size_t size) {
    return size <= SIZE_CLASS_SMALL_MAX;
}

static inline bool is_medium_class(size_t size) {
    return size > SIZE_CLASS_SMALL_MAX && size <= SIZE_CLASS_MEDIUM_MAX;
}

static inline bool is_large_class(size_t size) {
    return size > SIZE_CLASS_MEDIUM_MAX;
}

static inline bool is_small_class_idx(size_t class_idx) {
    return class_idx < NUM_SMALL_CLASSES;
}

static inline bool is_medium_class_idx(size_t class_idx) {
    return class_idx >= NUM_SMALL_CLASSES && class_idx < NUM_SIZE_CLASSES;
}

static inline bool is_large_class_idx(size_t class_idx) {
    return class_idx >= NUM_SIZE_CLASSES;
}

static inline size_t size_class_align(size_t size) {
    size_t page_size = get_page_size();
    if (size <= SIZE_CLASS_SMALL_MAX) {
        size_t class_idx = size_to_class_small(size);
        return small_size_classes[class_idx];
    } else if (size <= SIZE_CLASS_MEDIUM_MAX) {
        size_t class_idx = size_to_class_medium(size);
        return medium_size_classes[class_idx - NUM_SMALL_CLASSES];
    } else {
        return align_up(size, page_size);
    }
}

static inline size_t get_num_size_classes(void) {
    return NUM_SIZE_CLASSES;
}

static inline size_t get_num_small_classes(void) {
    return NUM_SMALL_CLASSES;
}

static inline size_t get_num_medium_classes(void) {
    return NUM_MEDIUM_CLASSES;
}

static inline size_t size_class_get_max_small(void) {
    return SIZE_CLASS_SMALL_MAX;
}

static inline size_t size_class_get_max_medium(void) {
    return SIZE_CLASS_MEDIUM_MAX;
}

static inline const size_t* get_small_size_classes(void) {
    return small_size_classes;
}

static inline const size_t* get_medium_size_classes(void) {
    return medium_size_classes;
}

void size_class_print_info(void);

#ifdef __cplusplus
}
#endif

#endif