#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <nomalloc/nomalloc.h>

#define TEST_PASS 0
#define TEST_FAIL 1

#define NUM_THREADS 8
#define ITERATIONS_PER_THREAD 10000

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

static void* thread_malloc_free(void* arg) {
    int thread_id = *(int*)arg;
    
    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        size_t size = 16 + (thread_id * 16) + (i % 64) * 16;
        if (size > 4096) size = 4096;
        
        void* ptr = malloc(size);
        if (!ptr) {
            fprintf(stderr, "Thread %d: malloc failed at iteration %d\n", thread_id, i);
            return (void*)TEST_FAIL;
        }
        
        memset(ptr, thread_id, size);
        
        for (size_t j = 0; j < size; j++) {
            if (((unsigned char*)ptr)[j] != (unsigned char)thread_id) {
                fprintf(stderr, "Thread %d: memory corruption detected\n", thread_id);
                free(ptr);
                return (void*)TEST_FAIL;
            }
        }
        
        free(ptr);
    }
    
    return (void*)TEST_PASS;
}

int test_concurrent_malloc_free(void) {
    nomalloc_init();
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_malloc_free, &thread_ids[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
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

static void* thread_alloc_hold(void* arg) {
    int thread_id = *(int*)arg;
    void* ptrs[100];
    
    for (int i = 0; i < 100; i++) {
        ptrs[i] = malloc(100 + i * 10);
        if (!ptrs[i]) {
            for (int j = 0; j < i; j++) {
                free(ptrs[j]);
            }
            return (void*)TEST_FAIL;
        }
        memset(ptrs[i], thread_id, 100 + i * 10);
    }
    
    for (int i = 0; i < 100; i++) {
        free(ptrs[i]);
    }
    
    return (void*)TEST_PASS;
}

int test_concurrent_hold_free(void) {
    nomalloc_init();
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_alloc_hold, &thread_ids[i]) != 0) {
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

static void* thread_realloc_test(void* arg) {
    int thread_id = *(int*)arg;
    
    for (int i = 0; i < 1000; i++) {
        void* ptr = malloc(100);
        if (!ptr) {
            return (void*)TEST_FAIL;
        }
        
        memset(ptr, thread_id, 100);
        
        ptr = realloc(ptr, 200);
        if (!ptr) {
            return (void*)TEST_FAIL;
        }
        
        for (int j = 0; j < 100; j++) {
            if (((unsigned char*)ptr)[j] != (unsigned char)thread_id) {
                free(ptr);
                return (void*)TEST_FAIL;
            }
        }
        
        ptr = realloc(ptr, 50);
        if (!ptr) {
            return (void*)TEST_FAIL;
        }
        
        free(ptr);
    }
    
    return (void*)TEST_PASS;
}

int test_concurrent_realloc(void) {
    nomalloc_init();
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_realloc_test, &thread_ids[i]) != 0) {
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

static void* thread_calloc_test(void* arg) {
    int thread_id = *(int*)arg;
    
    for (int i = 0; i < 1000; i++) {
        void* ptr = calloc(10, 100);
        if (!ptr) {
            return (void*)TEST_FAIL;
        }
        
        for (int j = 0; j < 1000; j++) {
            if (((unsigned char*)ptr)[j] != 0) {
                free(ptr);
                return (void*)TEST_FAIL;
            }
        }
        
        free(ptr);
    }
    
    return (void*)TEST_PASS;
}

int test_concurrent_calloc(void) {
    nomalloc_init();
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, thread_calloc_test, &thread_ids[i]) != 0) {
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

int main(void) {
    printf("\n=== Multi-thread SDV Tests ===\n\n");
    printf("Threads: %d, Iterations per thread: %d\n\n", NUM_THREADS, ITERATIONS_PER_THREAD);
    
    RUN_TEST(test_concurrent_malloc_free);
    RUN_TEST(test_concurrent_hold_free);
    RUN_TEST(test_concurrent_realloc);
    RUN_TEST(test_concurrent_calloc);
    
    printf("\n=== Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}