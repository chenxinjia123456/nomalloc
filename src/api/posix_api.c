#include <nomalloc/posix_api.h>
#include "../core/allocator.h"
#include "../core/size_class.h"
#include "../memory/chunk.h"
#include "../utils/memory.h"
#include "../utils/math.h"
#include "../utils/log.h"
#include "../utils/assert.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void* malloc(size_t size) {
    return allocator_malloc(size);
}

void free(void* ptr) {
    allocator_free(ptr);
}

void* calloc(size_t nmemb, size_t size) {
    return allocator_calloc(nmemb, size);
}

void* realloc(void* ptr, size_t size) {
    return allocator_realloc(ptr, size);
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    if (!memptr) {
        return EINVAL;
    }
    
    if (!is_power_of_two(alignment) || alignment < sizeof(void*)) {
        return EINVAL;
    }
    
    if (size == 0) {
        *memptr = NULL;
        return 0;
    }
    
    void* ptr = allocator_aligned_alloc(alignment, size);
    if (!ptr) {
        return ENOMEM;
    }
    
    *memptr = ptr;
    return 0;
}

void* aligned_alloc(size_t alignment, size_t size) {
    if (alignment == 0 || !is_power_of_two(alignment)) {
        return NULL;
    }
    
    if (size == 0) {
        size = 1;
    }
    
    size_t aligned_size = align_up(size, alignment);
    
    return allocator_aligned_alloc(alignment, aligned_size);
}

void* memalign(size_t alignment, size_t size) {
    if (alignment == 0 || !is_power_of_two(alignment)) {
        return NULL;
    }
    
    return allocator_aligned_alloc(alignment, size);
}

void* valloc(size_t size) {
    size_t page_size = get_page_size();
    return allocator_aligned_alloc(page_size, size);
}

void* pvalloc(size_t size) {
    size_t page_size = get_page_size();
    size_t aligned_size = align_up(size, page_size);
    return allocator_aligned_alloc(page_size, aligned_size);
}

size_t malloc_usable_size(void* ptr) {
    if (!ptr) {
        return 0;
    }
    
    return 0;
}

void* mallocx(size_t size, int flags) {
    size_t actual_size = size;
    
    if (flags & MALLOCX_ZERO) {
        void* ptr = allocator_malloc(actual_size);
        if (ptr) {
            memset(ptr, 0, actual_size);
        }
        return ptr;
    }
    
    return allocator_malloc(actual_size);
}

void dallocx(void* ptr, int flags) {
    allocator_free(ptr);
}

void sdallocx(void* ptr, size_t size, int flags) {
    allocator_free(ptr);
}

void* rallocx(void* ptr, size_t size, int flags) {
    return allocator_realloc(ptr, size);
}

size_t xallocx(void* ptr, size_t size, size_t extra, int flags) {
    if (!ptr) {
        return 0;
    }
    
    size_t old_size = malloc_usable_size(ptr);
    size_t new_size = size + extra;
    
    if (new_size <= old_size) {
        return old_size;
    }
    
    void* new_ptr = allocator_realloc(ptr, new_size);
    if (!new_ptr) {
        return old_size;
    }
    
    return malloc_usable_size(new_ptr);
}

size_t sallocx(void* ptr, int flags) {
    return malloc_usable_size(ptr);
}

size_t nallocx(size_t size, int flags) {
    if (size == 0) {
        return 0;
    }
    
    return size_class_align(size);
}