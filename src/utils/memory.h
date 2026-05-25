#ifndef NOMALLOC_UTILS_MEMORY_H
#define NOMALLOC_UTILS_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "math.h"
#include "assert.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PAGE_SIZE_DEFAULT 4096

static inline size_t get_page_size(void) {
    static size_t page_size = 0;
    if (page_size == 0) {
        page_size = (size_t)sysconf(_SC_PAGESIZE);
        if (page_size == 0) {
            page_size = PAGE_SIZE_DEFAULT;
        }
    }
    return page_size;
}

static inline void* memory_alloc_aligned(size_t size, size_t alignment) {
    nomalloc_assert(is_power_of_two(alignment), "alignment must be power of two");
    
    size_t page_size = get_page_size();
    size_t total_size = size + alignment;
    size_t aligned_size = align_up(total_size, page_size);
    
    void* raw_ptr = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (raw_ptr == MAP_FAILED) {
        return NULL;
    }
    
    uintptr_t addr = (uintptr_t)raw_ptr;
    uintptr_t aligned_addr = align_up(addr, alignment);
    size_t offset = aligned_addr - addr;
    
    if (offset > 0) {
        munmap(raw_ptr, offset);
    }
    
    size_t tail_size = aligned_size - offset - size;
    if (tail_size > 0) {
        munmap((void*)(aligned_addr + size), tail_size);
    }
    
    return (void*)aligned_addr;
}

static inline void memory_free_aligned(void* ptr, size_t size, size_t alignment) {
    if (ptr == NULL) return;
    
    size_t page_size = get_page_size();
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t page_aligned_addr = align_down(addr, page_size);
    
    size_t head_size = addr - page_aligned_addr;
    size_t tail_size = align_up(size, page_size) - size;
    size_t total_size = head_size + size + tail_size;
    
    munmap((void*)page_aligned_addr, align_up(total_size, page_size));
}

static inline void* memory_alloc_pages(size_t num_pages) {
    size_t page_size = get_page_size();
    size_t total_size = num_pages * page_size;
    
    void* ptr = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    return (ptr == MAP_FAILED) ? NULL : ptr;
}

static inline void memory_free_pages(void* ptr, size_t num_pages) {
    if (ptr == NULL) return;
    
    size_t page_size = get_page_size();
    munmap(ptr, num_pages * page_size);
}

static inline void* memory_alloc_huge_pages(size_t size) {
    size_t huge_page_size = 2 * 1024 * 1024;
    size_t aligned_size = align_up(size, huge_page_size);
    
    void* ptr = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    
    if (ptr == MAP_FAILED) {
        ptr = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        
        if (ptr == MAP_FAILED) {
            return NULL;
        }
        
        madvise(ptr, aligned_size, MADV_HUGEPAGE);
    }
    
    return ptr;
}

static inline void memory_free_huge_pages(void* ptr, size_t size) {
    if (ptr == NULL) return;
    
    size_t huge_page_size = 2 * 1024 * 1024;
    size_t aligned_size = align_up(size, huge_page_size);
    munmap(ptr, aligned_size);
}

static inline int memory_protect_read(void* ptr, size_t size) {
    return mprotect(ptr, size, PROT_READ);
}

static inline int memory_protect_write(void* ptr, size_t size) {
    return mprotect(ptr, size, PROT_WRITE);
}

static inline int memory_protect_read_write(void* ptr, size_t size) {
    return mprotect(ptr, size, PROT_READ | PROT_WRITE);
}

static inline int memory_protect_none(void* ptr, size_t size) {
    return mprotect(ptr, size, PROT_NONE);
}

static inline int memory_advise_random(void* ptr, size_t size) {
    return madvise(ptr, size, MADV_RANDOM);
}

static inline int memory_advise_sequential(void* ptr, size_t size) {
    return madvise(ptr, size, MADV_SEQUENTIAL);
}

static inline int memory_advise_will_need(void* ptr, size_t size) {
    return madvise(ptr, size, MADV_WILLNEED);
}

static inline int memory_advise_dont_need(void* ptr, size_t size) {
    return madvise(ptr, size, MADV_DONTNEED);
}

static inline int memory_lock(void* ptr, size_t size) {
    return mlock(ptr, size);
}

static inline int memory_unlock(void* ptr, size_t size) {
    return munlock(ptr, size);
}

static inline void memory_zero(void* ptr, size_t size) {
    memset(ptr, 0, size);
}

static inline void memory_fill(void* ptr, size_t size, uint8_t value) {
    memset(ptr, value, size);
}

static inline void memory_copy(void* dst, const void* src, size_t size) {
    memcpy(dst, src, size);
}

static inline int memory_compare(const void* ptr1, const void* ptr2, size_t size) {
    return memcmp(ptr1, ptr2, size);
}

static inline void* memory_find(const void* ptr, uint8_t value, size_t size) {
    return memchr(ptr, value, size);
}

static inline bool memory_is_zero(const void* ptr, size_t size) {
    const uint8_t* bytes = (const uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

static inline void memory_pattern_fill(void* ptr, const void* pattern, 
                                        size_t pattern_size, size_t total_size) {
    if (pattern_size == 0 || total_size == 0) return;
    
    uint8_t* dst = (uint8_t*)ptr;
    const uint8_t* src = (const uint8_t*)pattern;
    
    size_t copied = 0;
    while (copied < total_size) {
        size_t to_copy = min(pattern_size, total_size - copied);
        memory_copy(dst + copied, src, to_copy);
        copied += to_copy;
    }
}

#ifdef __cplusplus
}
#endif

#endif