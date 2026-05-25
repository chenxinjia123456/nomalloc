#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <nomalloc/nomalloc.h>

#define TEST_PASS 0
#define TEST_FAIL 1

#define NUM_THREADS 8
#define ITERATIONS 10000
#define MAX_PTRS 1000

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
} while(0)

static int test_basic_operations(void) {
    nomalloc_init();
    
    void* ptr = malloc(100);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    memset(ptr, 0x55, 100);
    for (int i = 0; i < 100; i++) {
        if (((unsigned char*)ptr)[i] != 0x55) {
            free(ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    free(ptr);
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_alignment(void) {
    nomalloc_init();
    
    void* ptrs[20];
    int count = 0;
    
    for (size_t alignment = 8; alignment <= 4096; alignment *= 2) {
        size_t size = alignment;
        void* ptr = aligned_alloc(alignment, size);
        if (ptr) {
            if ((uintptr_t)ptr % alignment != 0) {
                printf("Alignment failed: expected %zu, got %lu\n", 
                       alignment, (unsigned long)(uintptr_t)ptr % alignment);
                for (int i = 0; i < count; i++) free(ptrs[i]);
                free(ptr);
                nomalloc_shutdown();
                return TEST_FAIL;
            }
            ptrs[count++] = ptr;
        }
    }
    
    for (int i = 0; i < count; i++) {
        free(ptrs[i]);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_realloc_preserve(void) {
    nomalloc_init();
    
    void* ptr = malloc(100);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    memset(ptr, 0xAA, 100);
    
    ptr = realloc(ptr, 200);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    for (int i = 0; i < 100; i++) {
        if (((unsigned char*)ptr)[i] != 0xAA) {
            free(ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    free(ptr);
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_calloc_zero(void) {
    nomalloc_init();
    
    void* ptr = calloc(100, 10);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    for (int i = 0; i < 1000; i++) {
        if (((unsigned char*)ptr)[i] != 0) {
            free(ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    free(ptr);
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_null_free(void) {
    nomalloc_init();
    
    free(NULL);
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_zero_alloc(void) {
    nomalloc_init();
    
    void* ptr = malloc(0);
    if (ptr) {
        free(ptr);
    }
    
    ptr = calloc(0, 10);
    if (ptr) {
        free(ptr);
    }
    
    ptr = calloc(10, 0);
    if (ptr) {
        free(ptr);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_large_allocation(void) {
    nomalloc_init();
    
    size_t sizes[] = {1024 * 1024, 10 * 1024 * 1024, 100 * 1024 * 1024};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    for (int i = 0; i < num_sizes; i++) {
        void* ptr = malloc(sizes[i]);
        if (!ptr) {
            printf("Large allocation failed for size %zu\n", sizes[i]);
            continue;
        }
        
        memset(ptr, 0x77, sizes[i]);
        if (((unsigned char*)ptr)[0] != 0x77 || 
            ((unsigned char*)ptr)[sizes[i] - 1] != 0x77) {
            free(ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        
        free(ptr);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_many_allocations(void) {
    nomalloc_init();
    
    void* ptrs[MAX_PTRS];
    int count = 0;
    
    for (int i = 0; i < MAX_PTRS; i++) {
        ptrs[i] = malloc(64 + i);
        if (ptrs[i]) {
            memset(ptrs[i], i % 256, 64 + i);
            count++;
        }
    }
    
    for (int i = 0; i < MAX_PTRS; i++) {
        if (ptrs[i]) {
            free(ptrs[i]);
        }
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_fragmentation(void) {
    nomalloc_init();
    
    void* ptrs[100];
    
    for (int i = 0; i < 100; i++) {
        ptrs[i] = malloc(1000);
        if (!ptrs[i]) {
            for (int j = 0; j < i; j++) free(ptrs[j]);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    for (int i = 0; i < 100; i += 2) {
        free(ptrs[i]);
        ptrs[i] = NULL;
    }
    
    for (int i = 0; i < 100; i += 2) {
        ptrs[i] = malloc(500);
        if (!ptrs[i]) {
            for (int j = 0; j < 100; j++) {
                if (ptrs[j]) free(ptrs[j]);
            }
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) free(ptrs[i]);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_size_classes(void) {
    nomalloc_init();
    
    size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    void* ptrs[100];
    
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < num_sizes; i++) {
            for (int j = 0; j < 100; j++) {
                ptrs[j] = malloc(sizes[i]);
                if (!ptrs[j]) {
                    for (int k = 0; k < j; k++) free(ptrs[k]);
                    nomalloc_shutdown();
                    return TEST_FAIL;
                }
                memset(ptrs[j], 0x88, sizes[i]);
            }
            
            for (int j = 0; j < 100; j++) {
                free(ptrs[j]);
            }
        }
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static void* thread_reliability(void* arg) {
    int thread_id = *(int*)arg;
    void* ptrs[100];
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        int num_allocs = (rand() % 50) + 1;
        
        for (int i = 0; i < num_allocs; i++) {
            size_t size = (rand() % 4096) + 1;
            ptrs[i] = malloc(size);
            if (ptrs[i]) {
                memset(ptrs[i], thread_id + iter, size);
            }
        }
        
        for (int i = 0; i < num_allocs; i++) {
            if (ptrs[i]) {
                free(ptrs[i]);
                ptrs[i] = NULL;
            }
        }
    }
    
    return (void*)TEST_PASS;
}

static int test_multithread_reliability(void) {
    nomalloc_init();
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_reliability, &thread_ids[i]) != 0) {
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    int result = TEST_PASS;
    for (int i = 0; i < NUM_THREADS; i++) {
        void* thread_result;
        pthread_join(threads[i], &thread_result);
        if (thread_result != (void*)TEST_PASS) {
            result = TEST_FAIL;
        }
    }
    
    nomalloc_shutdown();
    return result;
}

static int test_realloc_null(void) {
    nomalloc_init();
    
    void* ptr = realloc(NULL, 100);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    memset(ptr, 0x99, 100);
    free(ptr);
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_realloc_zero(void) {
    nomalloc_init();
    
    void* ptr = malloc(100);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    ptr = realloc(ptr, 0);
    if (ptr) {
        free(ptr);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_double_free_detection(void) {
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

static int test_stress_realloc(void) {
    nomalloc_init();
    
    void* ptr = malloc(100);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    memset(ptr, 0xBB, 100);
    size_t current_data_size = 100;
    
    for (int i = 0; i < 100; i++) {
        size_t new_size = (rand() % 200) + 50;
        void* new_ptr = realloc(ptr, new_size);
        if (!new_ptr) {
            free(ptr);
            nomalloc_shutdown();
            return TEST_FAIL;
        }
        ptr = new_ptr;
        
        size_t check_size = current_data_size < new_size ? current_data_size : new_size;
        for (size_t j = 0; j < check_size; j++) {
            if (((unsigned char*)ptr)[j] != 0xBB) {
                printf("realloc data corruption at iteration %d, offset %zu, expected 0xBB got 0x%02X\n",
                       i, j, ((unsigned char*)ptr)[j]);
                printf("old_size=%zu, new_size=%zu, check_size=%zu\n", current_data_size, new_size, check_size);
                free(ptr);
                nomalloc_shutdown();
                return TEST_FAIL;
            }
        }
        
        memset(ptr, 0xBB, new_size);
        current_data_size = new_size;
    }
    
    free(ptr);
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_memalign_variations(void) {
    nomalloc_init();
    
    size_t alignments[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    int num_alignments = sizeof(alignments) / sizeof(alignments[0]);
    
    for (int i = 0; i < num_alignments; i++) {
        for (int j = 0; j < 100; j++) {
            size_t size = alignments[i] * (j + 1);
            void* ptr = aligned_alloc(alignments[i], size);
            
            if (ptr) {
                if ((uintptr_t)ptr % alignments[i] != 0) {
                    free(ptr);
                    nomalloc_shutdown();
                    return TEST_FAIL;
                }
                memset(ptr, 0xCC, size);
                free(ptr);
            }
        }
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_malloc_usable_size(void) {
    nomalloc_init();
    
    void* ptr = malloc(100);
    if (!ptr) {
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    size_t usable = malloc_usable_size(ptr);
    if (usable < 100) {
        printf("usable_size check failed: expected >= 100, got %zu\n", usable);
        free(ptr);
        nomalloc_shutdown();
        return TEST_FAIL;
    }
    
    free(ptr);
    nomalloc_shutdown();
    return TEST_PASS;
}

int main(void) {
    printf("\n=== Reliability Tests ===\n\n");
    
    srand(time(NULL));
    
    RUN_TEST(test_basic_operations);
    RUN_TEST(test_alignment);
    RUN_TEST(test_realloc_preserve);
    RUN_TEST(test_calloc_zero);
    RUN_TEST(test_null_free);
    RUN_TEST(test_zero_alloc);
    RUN_TEST(test_large_allocation);
    RUN_TEST(test_many_allocations);
    RUN_TEST(test_fragmentation);
    RUN_TEST(test_size_classes);
    RUN_TEST(test_multithread_reliability);
    RUN_TEST(test_realloc_null);
    RUN_TEST(test_realloc_zero);
    RUN_TEST(test_double_free_detection);
    RUN_TEST(test_stress_realloc);
    RUN_TEST(test_memalign_variations);
    RUN_TEST(test_malloc_usable_size);
    
    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n", 
           tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    
    return tests_failed > 0 ? 1 : 0;
}