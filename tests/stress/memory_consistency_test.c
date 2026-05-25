#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <assert.h>
#include <nomalloc/nomalloc.h>

#define TEST_PASS 0
#define TEST_FAIL 1

#define NUM_THREADS 8
#define ITERATIONS 50000
#define MAX_OBJECTS 1000
#define PATTERN_SIZE 32

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static atomic_int corruption_count = 0;
static atomic_int test_failed = 0;

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

typedef struct {
    unsigned char pattern[PATTERN_SIZE];
    size_t size;
    size_t thread_id;
    size_t seq_num;
} data_header_t;

static void write_pattern(void* ptr, size_t size, int thread_id, int seq) {
    if (!ptr || size < sizeof(data_header_t)) return;
    
    data_header_t* header = (data_header_t*)ptr;
    header->size = size;
    header->thread_id = thread_id;
    header->seq_num = seq;
    
    for (size_t i = 0; i < PATTERN_SIZE && i < size - sizeof(data_header_t); i++) {
        header->pattern[i] = (unsigned char)((thread_id + seq + i) & 0xFF);
    }
    
    unsigned char* tail = (unsigned char*)ptr + size - 16;
    for (size_t i = 0; i < 16; i++) {
        tail[i] = (unsigned char)((thread_id ^ seq ^ i) & 0xFF);
    }
}

static int verify_pattern(void* ptr, size_t size, int thread_id, int seq) {
    if (!ptr || size < sizeof(data_header_t)) return 0;
    
    data_header_t* header = (data_header_t*)ptr;
    
    if (header->size != size) {
        printf("Size mismatch: expected %zu, got %zu\n", size, header->size);
        return 0;
    }
    
    for (size_t i = 0; i < PATTERN_SIZE && i < size - sizeof(data_header_t); i++) {
        unsigned char expected = (unsigned char)((thread_id + seq + i) & 0xFF);
        if (header->pattern[i] != expected) {
            printf("Pattern mismatch at offset %zu: expected 0x%02X, got 0x%02X\n",
                   i, expected, header->pattern[i]);
            return 0;
        }
    }
    
    unsigned char* tail = (unsigned char*)ptr + size - 16;
    for (size_t i = 0; i < 16; i++) {
        unsigned char expected = (unsigned char)((thread_id ^ seq ^ i) & 0xFF);
        if (tail[i] != expected) {
            printf("Tail pattern mismatch at offset %zu\n", size - 16 + i);
            return 0;
        }
    }
    
    return 1;
}

static void* consistency_thread(void* arg) {
    int thread_id = *(int*)arg;
    void* ptrs[MAX_OBJECTS];
    size_t sizes[MAX_OBJECTS];
    int seqs[MAX_OBJECTS];
    int count = 0;
    
    for (int iter = 0; iter < ITERATIONS && atomic_load(&test_failed) == 0; iter++) {
        int op = rand() % 100;
        
        if (op < 60 && count < MAX_OBJECTS) {
            size_t sizes_arr[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};
            size_t size = sizes_arr[rand() % 8];
            
            void* ptr = malloc(size);
            if (ptr) {
                write_pattern(ptr, size, thread_id, iter);
                ptrs[count] = ptr;
                sizes[count] = size;
                seqs[count] = iter;
                count++;
            }
        } else if (op >= 60 && count > 0) {
            int idx = rand() % count;
            
            if (!verify_pattern(ptrs[idx], sizes[idx], thread_id, seqs[idx])) {
                atomic_fetch_add(&corruption_count, 1);
                atomic_store(&test_failed, 1);
            }
            
            free(ptrs[idx]);
            
            ptrs[idx] = ptrs[count - 1];
            sizes[idx] = sizes[count - 1];
            seqs[idx] = seqs[count - 1];
            count--;
        }
    }
    
    for (int i = 0; i < count; i++) {
        if (!verify_pattern(ptrs[i], sizes[i], thread_id, seqs[i])) {
            atomic_fetch_add(&corruption_count, 1);
        }
        free(ptrs[i]);
    }
    
    return NULL;
}

static int test_memory_consistency(void) {
    atomic_store(&corruption_count, 0);
    atomic_store(&test_failed, 0);
    
    nomalloc_init();
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, consistency_thread, &thread_ids[i]) != 0) {
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    nomalloc_shutdown();
    
    return atomic_load(&corruption_count) == 0 ? TEST_PASS : TEST_FAIL;
}

typedef struct {
    void* ptr;
    size_t size;
    atomic_int* shared;
    int pattern;
} shared_alloc_t;

static void* shared_reader_thread(void* arg) {
    shared_alloc_t* shared_alloc = (shared_alloc_t*)arg;
    
    for (int i = 0; i < ITERATIONS; i++) {
        if (atomic_load(shared_alloc->shared) == 1) {
            if (shared_alloc->ptr && shared_alloc->size > 0) {
                unsigned char* p = (unsigned char*)shared_alloc->ptr;
                for (size_t j = 0; j < shared_alloc->size && j < 256; j++) {
                    if (p[j] != (unsigned char)shared_alloc->pattern) {
                        atomic_fetch_add(&corruption_count, 1);
                        break;
                    }
                }
            }
        }
        usleep(1);
    }
    
    return NULL;
}

static void* shared_writer_thread(void* arg) {
    shared_alloc_t* shared_alloc = (shared_alloc_t*)arg;
    
    for (int i = 0; i < ITERATIONS / 10; i++) {
        void* new_ptr = malloc(shared_alloc->size);
        if (new_ptr) {
            memset(new_ptr, shared_alloc->pattern, shared_alloc->size);
            
            void* old_ptr = shared_alloc->ptr;
            shared_alloc->ptr = new_ptr;
            atomic_store(shared_alloc->shared, 1);
            
            usleep(10);
            
            if (old_ptr) {
                free(old_ptr);
            }
        }
    }
    
    return NULL;
}

static int test_shared_memory_access(void) {
    atomic_store(&corruption_count, 0);
    atomic_store(&test_failed, 0);
    
    nomalloc_init();
    
    shared_alloc_t shared_alloc = {
        .ptr = NULL,
        .size = 1024,
        .pattern = 0x42
    };
    atomic_int flag = 0;
    shared_alloc.shared = &flag;
    
    pthread_t readers[NUM_THREADS - 1];
    pthread_t writer;
    
    for (int i = 0; i < NUM_THREADS - 1; i++) {
        pthread_create(&readers[i], NULL, shared_reader_thread, &shared_alloc);
    }
    pthread_create(&writer, NULL, shared_writer_thread, &shared_alloc);
    
    for (int i = 0; i < NUM_THREADS - 1; i++) {
        pthread_join(readers[i], NULL);
    }
    pthread_join(writer, NULL);
    
    if (shared_alloc.ptr) {
        free(shared_alloc.ptr);
    }
    
    nomalloc_shutdown();
    
    return atomic_load(&corruption_count) == 0 ? TEST_PASS : TEST_FAIL;
}

static void* realloc_race_thread(void* arg) {
    int thread_id = *(int*)arg;
    void* ptrs[100];
    size_t sizes[100];
    int count = 0;
    
    for (int iter = 0; iter < ITERATIONS / 10; iter++) {
        if (count < 100) {
            size_t size = (rand() % 1024) + 64;
            void* ptr = malloc(size);
            if (ptr) {
                memset(ptr, (thread_id + iter) & 0xFF, size);
                ptrs[count] = ptr;
                sizes[count] = size;
                count++;
            }
        }
        
        if (count > 0) {
            int idx = rand() % count;
            size_t new_size = (rand() % 2048) + 64;
            void* new_ptr = realloc(ptrs[idx], new_size);
            
            if (new_ptr) {
                memset(new_ptr, (thread_id + iter) & 0xFF, new_size);
                ptrs[idx] = new_ptr;
                sizes[idx] = new_size;
            } else {
                sizes[idx] = 0;
            }
        }
        
        if (count > 50) {
            int idx = rand() % count;
            if (ptrs[idx]) {
                free(ptrs[idx]);
            }
            ptrs[idx] = ptrs[count - 1];
            sizes[idx] = sizes[count - 1];
            count--;
        }
    }
    
    for (int i = 0; i < count; i++) {
        if (ptrs[i]) {
            free(ptrs[i]);
        }
    }
    
    return NULL;
}

static int test_realloc_race(void) {
    nomalloc_init();
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, realloc_race_thread, &thread_ids[i]) != 0) {
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            nomalloc_shutdown();
            return TEST_FAIL;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_alignment_consistency(void) {
    nomalloc_init();
    
    size_t alignments[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    int num_alignments = sizeof(alignments) / sizeof(alignments[0]);
    
    for (int round = 0; round < 100; round++) {
        for (int i = 0; i < num_alignments; i++) {
            void* ptrs[100];
            
            for (int j = 0; j < 100; j++) {
                size_t size = alignments[i] * (j % 10 + 1);
                ptrs[j] = aligned_alloc(alignments[i], size);
                
                if (ptrs[j]) {
                    if ((uintptr_t)ptrs[j] % alignments[i] != 0) {
                        printf("Alignment violation: expected %zu, ptr=%p\n",
                               alignments[i], ptrs[j]);
                        for (int k = 0; k <= j; k++) {
                            free(ptrs[k]);
                        }
                        nomalloc_shutdown();
                        return TEST_FAIL;
                    }
                    memset(ptrs[j], 0x77, size);
                }
            }
            
            for (int j = 0; j < 100; j++) {
                if (ptrs[j]) {
                    free(ptrs[j]);
                }
            }
        }
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

typedef struct {
    void** shared_ptr;
    size_t size;
    atomic_int* ready;
    atomic_int* done;
    int thread_id;
} cross_thread_data_t;

static void* cross_thread_dealloc(void* arg) {
    cross_thread_data_t* data = (cross_thread_data_t*)arg;
    
    while (atomic_load(data->ready) == 0) {
        usleep(1);
    }
    
    if (*data->shared_ptr) {
        memset(*data->shared_ptr, 0xBB, data->size);
        free(*data->shared_ptr);
        *data->shared_ptr = NULL;
    }
    
    atomic_store(data->done, 1);
    
    return NULL;
}

static int test_cross_thread_dealloc(void) {
    nomalloc_init();
    
    for (int iter = 0; iter < 100; iter++) {
        void* shared_ptr = malloc(256);
        if (!shared_ptr) {
            continue;
        }
        memset(shared_ptr, 0xAA, 256);
        
        atomic_int ready = 0;
        atomic_int done = 0;
        
        cross_thread_data_t data = {
            .shared_ptr = &shared_ptr,
            .size = 256,
            .ready = &ready,
            .done = &done,
            .thread_id = 0
        };
        
        pthread_t thread;
        pthread_create(&thread, NULL, cross_thread_dealloc, &data);
        
        memset(shared_ptr, 0x55, 256);
        atomic_store(&ready, 1);
        
        while (atomic_load(&done) == 0) {
            usleep(1);
        }
        
        pthread_join(thread, NULL);
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static void* high_contention_thread(void* arg) {
    void** global_pool = (void**)arg;
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        int idx = rand() % MAX_OBJECTS;
        
        void* ptr = __atomic_exchange_n(&global_pool[idx], NULL, __ATOMIC_SEQ_CST);
        
        if (ptr) {
            memset(ptr, 0xDD, 128);
            free(ptr);
        }
        
        void* new_ptr = malloc(128);
        if (new_ptr) {
            memset(new_ptr, 0xEE, 128);
            __atomic_exchange_n(&global_pool[idx], new_ptr, __ATOMIC_SEQ_CST);
        }
    }
    
    return NULL;
}

static int test_high_contention(void) {
    nomalloc_init();
    
    void* global_pool[MAX_OBJECTS];
    memset(global_pool, 0, sizeof(global_pool));
    
    for (int i = 0; i < MAX_OBJECTS / 2; i++) {
        global_pool[i] = malloc(128);
        if (global_pool[i]) {
            memset(global_pool[i], 0xCC, 128);
        }
    }
    
    pthread_t threads[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, high_contention_thread, global_pool);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    for (int i = 0; i < MAX_OBJECTS; i++) {
        if (global_pool[i]) {
            free(global_pool[i]);
        }
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

static int test_stress_usable_size(void) {
    nomalloc_init();
    
    for (int iter = 0; iter < 1000; iter++) {
        size_t sizes[] = {1, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
        int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
        
        void* ptrs[100];
        size_t usable[100];
        int count = 0;
        
        for (int i = 0; i < 100; i++) {
            size_t size = sizes[rand() % num_sizes];
            ptrs[i] = malloc(size);
            if (ptrs[i]) {
                usable[i] = malloc_usable_size(ptrs[i]);
                if (usable[i] < size) {
                    printf("usable_size error: requested %zu, usable %zu\n", size, usable[i]);
                    for (int j = 0; j <= i; j++) {
                        free(ptrs[j]);
                    }
                    nomalloc_shutdown();
                    return TEST_FAIL;
                }
                count++;
            }
        }
        
        for (int i = 0; i < count; i++) {
            size_t new_size = (rand() % 8192) + 1;
            void* new_ptr = realloc(ptrs[i], new_size);
            if (new_ptr) {
                ptrs[i] = new_ptr;
                usable[i] = malloc_usable_size(new_ptr);
                if (usable[i] < new_size) {
                    printf("realloc usable_size error: requested %zu, usable %zu\n",
                           new_size, usable[i]);
                    for (int j = 0; j < count; j++) {
                        free(ptrs[j]);
                    }
                    nomalloc_shutdown();
                    return TEST_FAIL;
                }
            }
        }
        
        for (int i = 0; i < count; i++) {
            if (ptrs[i]) {
                free(ptrs[i]);
            }
        }
    }
    
    nomalloc_shutdown();
    return TEST_PASS;
}

int main(void) {
    printf("\n========================================\n");
    printf("   Memory Consistency Test Suite\n");
    printf("========================================\n\n");
    
    srand(time(NULL));
    
    RUN_TEST(test_memory_consistency);
    RUN_TEST(test_shared_memory_access);
    RUN_TEST(test_realloc_race);
    RUN_TEST(test_alignment_consistency);
    RUN_TEST(test_cross_thread_dealloc);
    RUN_TEST(test_high_contention);
    RUN_TEST(test_stress_usable_size);
    
    printf("\n========================================\n");
    printf("   Results Summary\n");
    printf("========================================\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Success rate: %.1f%%\n",
           tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    
    return tests_failed > 0 ? 1 : 0;
}