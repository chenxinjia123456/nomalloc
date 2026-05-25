#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <nomalloc/nomalloc.h>

#define TEST_PASS 0
#define TEST_FAIL 1

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(test) do { \
    tests_run++; \
    printf("Running %s... ", #test); \
    if (test() == TEST_PASS) { \
        tests_passed++; \
        printf("PASS\n"); \
    } else { \
        tests_failed++; \
        printf("FAIL\n"); \
    } \
} while(0)

int test_large_allocations(void) {
    nomalloc_init();
    
    size_t large_sizes[] = {
        65536, 131072, 262144, 524288, 1048576, 2097152, 4194304, 8388608
    };
    size_t num_sizes = sizeof(large_sizes) / sizeof(large_sizes[0]);
    
    for (size_t i = 0; i < num_sizes; i++) {
        void* ptr = malloc(large_sizes[i]);
        if (!ptr) {
            printf("Failed at size %zu\n", large_sizes[i]);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        memset(ptr, 0xFF, large_sizes[i]);
        free(ptr);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_many_small_allocations(void) {
    nomalloc_init();
    
    void* ptrs[10000];
    size_t count = 0;
    
    for (size_t i = 0; i < 10000; i++) {
        ptrs[i] = malloc(16 + (i % 64) * 16);
        if (!ptrs[i]) {
            break;
        }
        count++;
        memset(ptrs[i], 0xAA, 16 + (i % 64) * 16);
    }
    
    printf("Allocated %zu small blocks\n", count);
    
    for (size_t i = 0; i < count; i++) {
        free(ptrs[i]);
    }
    
    nomalloc_shutdown();
    return count > 5000 ? TEST_PASS : TEST_FAIL;
}

int test_alloc_free_cycle(void) {
    nomalloc_init();
    
    for (int cycle = 0; cycle < 100; cycle++) {
        void* ptrs[1000];
        
        for (int i = 0; i < 1000; i++) {
            ptrs[i] = malloc(32 + i * 8);
            if (!ptrs[i]) {
                for (int j = 0; j < i; j++) {
                    free(ptrs[j]);
                }
                nomalloc_shutdown();
                return TEST_FAIL;
            }
            memset(ptrs[i], cycle, 32 + i * 8);
        }
        
        for (int i = 0; i < 1000; i++) {
            free(ptrs[i]);
        }
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_random_sizes(void) {
    nomalloc_init();
    
    srand(12345);
    
    void* ptrs[1000];
    size_t sizes[1000];
    
    for (int i = 0; i < 1000; i++) {
        sizes[i] = 8 + rand() % 4096;
        ptrs[i] = malloc(sizes[i]);
        if (!ptrs[i]) {
            for (int j = 0; j < i; j++) {
                free(ptrs[j]);
            }
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        memset(ptrs[i], rand(), sizes[i]);
    }
    
    for (int i = 0; i < 1000; i++) {
        free(ptrs[i]);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_alternating_sizes(void) {
    nomalloc_init();
    
    void* ptr_small = NULL;
    void* ptr_large = NULL;
    
    for (int i = 0; i < 1000; i++) {
        ptr_small = malloc(16);
        if (!ptr_small) {
            if (ptr_large) free(ptr_large);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        memset(ptr_small, 0xAA, 16);
        
        ptr_large = malloc(1048576);
        if (!ptr_large) {
            free(ptr_small);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        memset(ptr_large, 0xBB, 1048576);
        
        free(ptr_small);
        free(ptr_large);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_fragmentation_pattern(void) {
    nomalloc_init();
    
    void* ptrs[100];
    
    for (int i = 0; i < 100; i++) {
        ptrs[i] = malloc(1024);
        if (!ptrs[i]) {
            for (int j = 0; j < i; j++) {
                free(ptrs[j]);
            }
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        memset(ptrs[i], i, 1024);
    }
    
    for (int i = 0; i < 100; i += 2) {
        free(ptrs[i]);
        ptrs[i] = NULL;
    }
    
    for (int i = 1; i < 100; i += 2) {
        free(ptrs[i]);
        ptrs[i] = NULL;
    }
    
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 100; i++) {
            ptrs[i] = malloc(512);
            if (!ptrs[i]) {
                for (int j = 0; j < i; j++) {
                    if (ptrs[j]) free(ptrs[j]);
                }
                nomalloc_shutdown();
                return TEST_FAIL;
            }
            memset(ptrs[i], round, 512);
        }
        
        for (int i = 0; i < 100; i++) {
            free(ptrs[i]);
            ptrs[i] = NULL;
        }
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_realloc_grow_shrink(void) {
    nomalloc_init();
    
    void* ptr = malloc(16);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    memset(ptr, 0xAA, 16);
    
    for (int i = 0; i < 100; i++) {
        size_t new_size = 16 + i * 16;
        ptr = realloc(ptr, new_size);
        if (!ptr) {
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        for (int j = 0; j < 16; j++) {
            if (((unsigned char*)ptr)[j] != 0xAA) {
                free(ptr);
                nomalloc_shutdown();
                return TEST_FAIL;
            }
        }
        
        memset(ptr, 0xBB, new_size);
    }
    
    for (int i = 99; i >= 0; i--) {
        size_t new_size = 16 + i * 16;
        ptr = realloc(ptr, new_size);
        if (!ptr) {
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        for (int j = 0; j < new_size; j++) {
            if (((unsigned char*)ptr)[j] != 0xBB) {
                free(ptr);
                nomalloc_shutdown();
                return TEST_FAIL;
            }
        }
    }
    
    free(ptr);
    nomalloc_shutdown();
    return TEST_PASS;
}

int test_memory_pressure(void) {
    nomalloc_init();
    
    void* ptrs[1000];
    size_t allocated = 0;
    
    for (int i = 0; i < 1000; i++) {
        ptrs[i] = malloc(1048576);
        if (!ptrs[i]) {
            break;
        }
        allocated += 1048576;
        memset(ptrs[i], 0xAA, 1048576);
    }
    
    printf("Allocated %zu MB under pressure\n", allocated / 1048576);
    
    for (int i = 0; i < 1000; i++) {
        if (ptrs[i]) free(ptrs[i]);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int main(void) {
    printf("\n=== Stress SDV Tests ===\n\n");
    
    RUN_TEST(test_large_allocations);
    RUN_TEST(test_many_small_allocations);
    RUN_TEST(test_alloc_free_cycle);
    RUN_TEST(test_random_sizes);
    RUN_TEST(test_alternating_sizes);
    RUN_TEST(test_fragmentation_pattern);
    RUN_TEST(test_realloc_grow_shrink);
    RUN_TEST(test_memory_pressure);
    
    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}