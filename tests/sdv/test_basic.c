#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    fflush(stdout); \
    if (test() == TEST_PASS) { \
        tests_passed++; \
        printf("PASS\n"); \
    } else { \
        tests_failed++; \
        printf("FAIL\n"); \
    } \
    fflush(stdout); \
} while(0)

int test_version(void) {
    const char* version = nomalloc_version();
    if (!version || strlen(version) == 0) {
        return TEST_FAIL;
    }
    printf("Version: %s\n", version);
    return TEST_PASS;
}

int test_build_info(void) {
    const char* build_info = nomalloc_build_info();
    if (!build_info || strlen(build_info) == 0) {
        return TEST_FAIL;
    }
    printf("Build info: %s\n", build_info);
    return TEST_PASS;
}

int test_init(void) {
    if (nomalloc_init() != 0) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

int test_basic_malloc_free(void) {
    void* ptr = malloc(100);
    if (!ptr) {
        return TEST_FAIL;
    }
    
    memset(ptr, 0xAA, 100);
    free(ptr);
    
    return TEST_PASS;
}

int test_malloc_sizes(void) {
    size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    for (size_t i = 0; i < num_sizes; i++) {
        void* ptr = malloc(sizes[i]);
        if (!ptr) {
            printf("Failed at size %zu\n", sizes[i]);
            return TEST_FAIL;
        }
        memset(ptr, 0xBB, sizes[i]);
        free(ptr);
    }
    
    return TEST_PASS;
}

int test_calloc(void) {
    void* ptr = calloc(10, 100);
    if (!ptr) {
        return TEST_FAIL;
    }
    
    for (int i = 0; i < 1000; i++) {
        if (((char*)ptr)[i] != 0) {
            free(ptr);
            return TEST_FAIL;
        }
    }
    
    free(ptr);
    return TEST_PASS;
}

int test_realloc(void) {
    void* ptr = malloc(100);
    if (!ptr) {
        return TEST_FAIL;
    }
    
    memset(ptr, 0xCC, 100);
    
    ptr = realloc(ptr, 200);
    if (!ptr) {
        return TEST_FAIL;
    }
    
    for (int i = 0; i < 100; i++) {
        if (((char*)ptr)[i] != 0xCC) {
            free(ptr);
            return TEST_FAIL;
        }
    }
    
    memset(ptr, 0xDD, 200);
    
    ptr = realloc(ptr, 50);
    if (!ptr) {
        return TEST_FAIL;
    }
    
    free(ptr);
    return TEST_PASS;
}

int test_realloc_null(void) {
    void* ptr = realloc(NULL, 100);
    if (!ptr) {
        return TEST_FAIL;
    }
    
    memset(ptr, 0xEE, 100);
    free(ptr);
    
    return TEST_PASS;
}

int test_aligned_alloc(void) {
    size_t alignments[] = {16, 32, 64, 128, 256, 512, 1024, 4096};
    size_t num_alignments = sizeof(alignments) / sizeof(alignments[0]);
    
    for (size_t i = 0; i < num_alignments; i++) {
        void* ptr = aligned_alloc(alignments[i], alignments[i]);
        if (!ptr) {
            printf("Failed at alignment %zu\n", alignments[i]);
            return TEST_FAIL;
        }
        
        uintptr_t addr = (uintptr_t)ptr;
        if (addr % alignments[i] != 0) {
            printf("Alignment failed: addr=%p, alignment=%zu\n", ptr, alignments[i]);
            free(ptr);
            return TEST_FAIL;
        }
        
        memset(ptr, 0xFF, alignments[i]);
        free(ptr);
    }
    
    return TEST_PASS;
}

int test_multiple_allocations(void) {
    void* ptrs[100];
    
    for (int i = 0; i < 100; i++) {
        ptrs[i] = malloc(64);
        if (!ptrs[i]) {
            for (int j = 0; j < i; j++) {
                free(ptrs[j]);
            }
            return TEST_FAIL;
        }
    }
    
    for (int i = 0; i < 100; i++) {
        memset(ptrs[i], i, 64);
    }
    
    for (int i = 0; i < 100; i++) {
        free(ptrs[i]);
    }
    
    return TEST_PASS;
}

int test_free_null(void) {
    free(NULL);
    return TEST_PASS;
}

int main(void) {
    printf("\n=== Basic SDV Tests ===\n\n");
    fflush(stdout);
    
    RUN_TEST(test_version);
    RUN_TEST(test_build_info);
    RUN_TEST(test_init);
    RUN_TEST(test_basic_malloc_free);
    RUN_TEST(test_malloc_sizes);
    RUN_TEST(test_calloc);
    RUN_TEST(test_realloc_null);
    RUN_TEST(test_multiple_allocations);
    RUN_TEST(test_free_null);
    
    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    fflush(stdout);
    
    return tests_failed;
}