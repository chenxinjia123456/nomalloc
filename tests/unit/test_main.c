#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <nomalloc/nomalloc.h>
#include "../src/core/allocator.h"
#include "../src/utils/assert.h"

#define TEST_PASS 0
#define TEST_FAIL 1

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(test_func) do { \
    tests_run++; \
    printf("Running test: %s...\n", #test_func); \
    if (test_func() == TEST_PASS) { \
        tests_passed++; \
        printf("  PASSED\n"); \
    } else { \
        tests_failed++; \
        printf("  FAILED\n"); \
    } \
} while (0)

int test_init_shutdown(void) {
    if (nomalloc_init() != 0) {
        return TEST_FAIL;
    }
    
    if (!allocator_is_initialized()) {
        return TEST_FAIL;
    }
    
    nomalloc_shutdown();
    
    if (allocator_is_initialized()) {
        return TEST_FAIL;
    }
    
    return TEST_PASS;
}

int test_malloc_free_basic(void) {
    nomalloc_init();
    
    void* ptr = malloc(100);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    free(ptr);
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_malloc_free_sizes(void) {
    nomalloc_init();
    
    size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 
                      8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576};
    size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    for (size_t i = 0; i < num_sizes; i++) {
        void* ptr = malloc(sizes[i]);
        if (!ptr) {
            printf("  Failed to allocate %zu bytes\n", sizes[i]);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        memset(ptr, 0xAA, sizes[i]);
        
        size_t usable_size = malloc_usable_size(ptr);
        if (usable_size < sizes[i]) {
            printf("  Usable size too small: expected >= %zu, got %zu\n",
                   sizes[i], usable_size);
            free(ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        free(ptr);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_calloc(void) {
    nomalloc_init();
    
    size_t nmemb = 10;
    size_t size = 100;
    
    void* ptr = calloc(nmemb, size);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    unsigned char* bytes = (unsigned char*)ptr;
    for (size_t i = 0; i < nmemb * size; i++) {
        if (bytes[i] != 0) {
            printf("  Memory not zeroed at index %zu\n", i);
            free(ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    free(ptr);
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_realloc(void) {
    nomalloc_init();
    
    size_t initial_size = 100;
    size_t new_size = 200;
    
    void* ptr = malloc(initial_size);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    memset(ptr, 0xBB, initial_size);
    
    void* new_ptr = realloc(ptr, new_size);
    if (!new_ptr) {
        free(ptr);
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    unsigned char* bytes = (unsigned char*)new_ptr;
    for (size_t i = 0; i < initial_size; i++) {
        if (bytes[i] != 0xBB) {
            printf("  Data not preserved at index %zu\n", i);
            free(new_ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    free(new_ptr);
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_realloc_null(void) {
    nomalloc_init();
    
    void* ptr = realloc(NULL, 100);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    free(ptr);
    
    ptr = malloc(100);
    void* new_ptr = realloc(ptr, 0);
    
    if (new_ptr != NULL) {
        printf("  realloc(ptr, 0) should return NULL\n");
        free(new_ptr);
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_aligned_alloc(void) {
    nomalloc_init();
    
    size_t alignments[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    size_t num_alignments = sizeof(alignments) / sizeof(alignments[0]);
    
    for (size_t i = 0; i < num_alignments; i++) {
        size_t alignment = alignments[i];
        size_t size = alignment * 2;
        
        void* ptr = aligned_alloc(alignment, size);
        if (!ptr) {
            printf("  Failed to allocate aligned memory: alignment=%zu, size=%zu\n",
                   alignment, size);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        uintptr_t addr = (uintptr_t)ptr;
        if (addr % alignment != 0) {
            printf("  Memory not aligned: expected alignment %zu, got address %p\n",
                   alignment, ptr);
            free(ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        free(ptr);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_posix_memalign(void) {
    nomalloc_init();
    
    size_t alignments[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    size_t num_alignments = sizeof(alignments) / sizeof(alignments[0]);
    
    for (size_t i = 0; i < num_alignments; i++) {
        size_t alignment = alignments[i];
        size_t size = 100;
        
        void* ptr = NULL;
        int ret = posix_memalign(&ptr, alignment, size);
        
        if (ret != 0 || !ptr) {
            printf("  posix_memalign failed: alignment=%zu, ret=%d\n",
                   alignment, ret);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        uintptr_t addr = (uintptr_t)ptr;
        if (addr % alignment != 0) {
            printf("  Memory not aligned: expected alignment %zu, got address %p\n",
                   alignment, ptr);
            free(ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        free(ptr);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_large_allocation(void) {
    nomalloc_init();
    
    size_t large_sizes[] = {2 * 1024 * 1024, 4 * 1024 * 1024, 8 * 1024 * 1024};
    size_t num_sizes = sizeof(large_sizes) / sizeof(large_sizes[0]);
    
    for (size_t i = 0; i < num_sizes; i++) {
        void* ptr = malloc(large_sizes[i]);
        if (!ptr) {
            printf("  Failed to allocate large memory: size=%zu\n", large_sizes[i]);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        memset(ptr, 0xCC, large_sizes[i]);
        free(ptr);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_multiple_allocations(void) {
    nomalloc_init();
    
    size_t num_allocs = 1000;
    void** ptrs = (void**)malloc(num_allocs * sizeof(void*));
    
    if (!ptrs) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    for (size_t i = 0; i < num_allocs; i++) {
        ptrs[i] = malloc(100 + i);
        if (!ptrs[i]) {
            printf("  Failed allocation %zu\n", i);
            for (size_t j = 0; j < i; j++) {
                free(ptrs[j]);
            }
            free(ptrs);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    for (size_t i = 0; i < num_allocs; i++) {
        free(ptrs[i]);
    }
    
    free(ptrs);
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_free_null(void) {
    nomalloc_init();
    
    free(NULL);
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_malloc_zero(void) {
    nomalloc_init();
    
    void* ptr = malloc(0);
    
    free(ptr);
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_statistics(void) {
    nomalloc_init();
    
    size_t num_allocs = 100;
    size_t alloc_size = 1000;
    
    for (size_t i = 0; i < num_allocs; i++) {
        void* ptr = malloc(alloc_size);
        if (!ptr) {
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        free(ptr);
    }
    
    size_t total_allocated = nomalloc_get_total_allocated();
    size_t total_freed = nomalloc_get_total_freed();
    
    if (total_allocated == 0 || total_freed == 0) {
        printf("  Statistics not updated: allocated=%zu, freed=%zu\n",
               total_allocated, total_freed);
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

void* thread_alloc_func(void* arg) {
    size_t num_allocs = *((size_t*)arg);
    
    for (size_t i = 0; i < num_allocs; i++) {
        void* ptr = malloc(100);
        if (ptr) {
            memset(ptr, 0xDD, 100);
            free(ptr);
        }
    }
    
    return NULL;
}

int test_multithreaded(void) {
    nomalloc_init();
    
    size_t num_threads = 4;
    size_t num_allocs_per_thread = 1000;
    
    pthread_t threads[num_threads];
    
    for (size_t i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, thread_alloc_func, 
                           &num_allocs_per_thread) != 0) {
            printf("  Failed to create thread %zu\n", i);
            for (size_t j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    for (size_t i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int main(void) {
    printf("=== Nomalloc Unit Tests ===\n\n");
    
    RUN_TEST(test_init_shutdown);
    RUN_TEST(test_malloc_free_basic);
    RUN_TEST(test_malloc_free_sizes);
    RUN_TEST(test_calloc);
    RUN_TEST(test_realloc);
    RUN_TEST(test_realloc_null);
    RUN_TEST(test_aligned_alloc);
    RUN_TEST(test_posix_memalign);
    RUN_TEST(test_large_allocation);
    RUN_TEST(test_multiple_allocations);
    RUN_TEST(test_free_null);
    RUN_TEST(test_malloc_zero);
    RUN_TEST(test_statistics);
    RUN_TEST(test_multithreaded);
    
    printf("\n=== Test Summary ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    if (tests_failed > 0) {
        printf("\nSome tests FAILED!\n");
        return 1;
    }
    
    printf("\nAll tests PASSED!\n");
    return 0;
}