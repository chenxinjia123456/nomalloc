#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <nomalloc/nomalloc.h>

#define NUM_THREADS 16
#define TEST_DURATION_SEC 30
#define MAX_OBJECTS_PER_THREAD 10000
#define CACHE_LINE_SIZE 64

typedef struct {
    atomic_long alloc_count;
    atomic_long free_count;
    atomic_long realloc_count;
    atomic_long alloc_failures;
    atomic_long data_corruptions;
    atomic_long race_detected;
} global_stats_t;

typedef struct {
    int thread_id;
    void* objects[MAX_OBJECTS_PER_THREAD];
    size_t sizes[MAX_OBJECTS_PER_THREAD];
    unsigned char patterns[MAX_OBJECTS_PER_THREAD];
    size_t count;
    size_t operations;
    size_t corrupted;
} thread_local_t;

static global_stats_t global_stats;
static atomic_int running = 1;
static atomic_int barrier = 0;

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void barrier_wait(int n) {
    atomic_fetch_add(&barrier, 1);
    while (atomic_load(&barrier) < n) {
        __asm__ volatile("pause" ::: "memory");
    }
}

static void verify_pattern(void* ptr, size_t size, unsigned char pattern) {
    if (!ptr || size == 0) return;
    unsigned char* p = (unsigned char*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (p[i] != pattern) {
            atomic_fetch_add(&global_stats.data_corruptions, 1);
            return;
        }
    }
}

static void* concurrent_allocator_thread(void* arg) {
    thread_local_t* local = (thread_local_t*)arg;
    local->count = 0;
    local->operations = 0;
    local->corrupted = 0;
    
    barrier_wait(NUM_THREADS);
    
    while (atomic_load(&running)) {
        int op = rand() % 100;
        
        if (op < 45 && local->count < MAX_OBJECTS_PER_THREAD) {
            size_t size_classes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
            int class_idx = rand() % (sizeof(size_classes) / sizeof(size_classes[0]));
            size_t size = size_classes[class_idx];
            
            void* ptr = malloc(size);
            if (ptr) {
                unsigned char pattern = (unsigned char)(local->thread_id & 0xFF);
                memset(ptr, pattern, size);
                
                local->objects[local->count] = ptr;
                local->sizes[local->count] = size;
                local->patterns[local->count] = pattern;
                local->count++;
                
                atomic_fetch_add(&global_stats.alloc_count, 1);
            } else {
                atomic_fetch_add(&global_stats.alloc_failures, 1);
            }
        } else if (op < 50 && local->count < MAX_OBJECTS_PER_THREAD) {
            size_t size = (rand() % (1024 * 1024)) + 4096;
            
            void* ptr = malloc(size);
            if (ptr) {
                unsigned char pattern = (unsigned char)((local->thread_id | 0x80) & 0xFF);
                memset(ptr, pattern, size > 1024 ? 1024 : size);
                
                local->objects[local->count] = ptr;
                local->sizes[local->count] = size;
                local->patterns[local->count] = pattern;
                local->count++;
                
                atomic_fetch_add(&global_stats.alloc_count, 1);
            } else {
                atomic_fetch_add(&global_stats.alloc_failures, 1);
            }
        } else if (op < 60 && local->count > 0) {
            size_t idx = rand() % local->count;
            void* old_ptr = local->objects[idx];
            size_t old_size = local->sizes[idx];
            unsigned char old_pattern = local->patterns[idx];
            
            if (old_ptr) {
                verify_pattern(old_ptr, old_size > 128 ? 128 : old_size, old_pattern);
                
                size_t new_size = (rand() % 8192) + 1;
                void* new_ptr = realloc(old_ptr, new_size);
                
                if (new_ptr) {
                    unsigned char new_pattern = (unsigned char)(local->thread_id & 0xFF);
                    memset(new_ptr, new_pattern, new_size);
                    local->objects[idx] = new_ptr;
                    local->sizes[idx] = new_size;
                    local->patterns[idx] = new_pattern;
                    atomic_fetch_add(&global_stats.realloc_count, 1);
                } else {
                    atomic_fetch_add(&global_stats.alloc_failures, 1);
                }
            }
        } else if (op < 95 && local->count > 0) {
            size_t idx = rand() % local->count;
            void* ptr = local->objects[idx];
            
            if (ptr) {
                free(ptr);
                atomic_fetch_add(&global_stats.free_count, 1);
            }
            
            local->objects[idx] = local->objects[local->count - 1];
            local->sizes[idx] = local->sizes[local->count - 1];
            local->patterns[idx] = local->patterns[local->count - 1];
            local->count--;
        } else if (op < 98 && local->count < MAX_OBJECTS_PER_THREAD) {
            size_t alignments[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
            int align_idx = rand() % (sizeof(alignments) / sizeof(alignments[0]));
            size_t alignment = alignments[align_idx];
            size_t size = alignment * (rand() % 10 + 1);
            
            void* ptr = aligned_alloc(alignment, size);
            if (ptr) {
                if ((uintptr_t)ptr % alignment != 0) {
                    atomic_fetch_add(&global_stats.race_detected, 1);
                }
                
                unsigned char pattern = 0xAA;
                memset(ptr, pattern, size > 256 ? 256 : size);
                local->objects[local->count] = ptr;
                local->sizes[local->count] = size;
                local->patterns[local->count] = pattern;
                local->count++;
                
                atomic_fetch_add(&global_stats.alloc_count, 1);
            } else {
                atomic_fetch_add(&global_stats.alloc_failures, 1);
            }
        }
        
        local->operations++;
    }
    
    for (size_t i = 0; i < local->count; i++) {
        if (local->objects[i]) {
            free(local->objects[i]);
            atomic_fetch_add(&global_stats.free_count, 1);
        }
    }
    
    return NULL;
}

static void* concurrent_calloc_thread(void* arg) {
    thread_local_t* local = (thread_local_t*)arg;
    local->count = 0;
    local->operations = 0;
    
    barrier_wait(NUM_THREADS);
    
    while (atomic_load(&running)) {
        if (local->count < MAX_OBJECTS_PER_THREAD / 2) {
            size_t nmemb = rand() % 100 + 1;
            size_t size = rand() % 100 + 1;
            
            void* ptr = calloc(nmemb, size);
            if (ptr) {
                for (size_t i = 0; i < nmemb * size && i < 256; i++) {
                    if (((unsigned char*)ptr)[i] != 0) {
                        atomic_fetch_add(&global_stats.data_corruptions, 1);
                        break;
                    }
                }
                
                local->objects[local->count] = ptr;
                local->sizes[local->count] = nmemb * size;
                local->count++;
                
                atomic_fetch_add(&global_stats.alloc_count, 1);
            } else {
                atomic_fetch_add(&global_stats.alloc_failures, 1);
            }
        } else {
            size_t idx = rand() % local->count;
            if (local->objects[idx]) {
                free(local->objects[idx]);
                atomic_fetch_add(&global_stats.free_count, 1);
            }
            
            local->objects[idx] = local->objects[local->count - 1];
            local->sizes[idx] = local->sizes[local->count - 1];
            local->count--;
        }
        
        local->operations++;
    }
    
    for (size_t i = 0; i < local->count; i++) {
        if (local->objects[i]) {
            free(local->objects[i]);
            atomic_fetch_add(&global_stats.free_count, 1);
        }
    }
    
    return NULL;
}

static void* burst_allocator_thread(void* arg) {
    thread_local_t* local = (thread_local_t*)arg;
    local->count = 0;
    local->operations = 0;
    
    barrier_wait(NUM_THREADS);
    
    int burst_count = 0;
    
    while (atomic_load(&running)) {
        if (burst_count % 100 < 10) {
            for (int i = 0; i < 100 && local->count < MAX_OBJECTS_PER_THREAD; i++) {
                size_t size = (rand() % 4096) + 1;
                void* ptr = malloc(size);
                if (ptr) {
                    memset(ptr, 0x55, size);
                    local->objects[local->count] = ptr;
                    local->sizes[local->count] = size;
                    local->count++;
                    atomic_fetch_add(&global_stats.alloc_count, 1);
                } else {
                    atomic_fetch_add(&global_stats.alloc_failures, 1);
                }
            }
        } else {
            for (int i = 0; i < 100 && local->count > 0; i++) {
                size_t idx = rand() % local->count;
                if (local->objects[idx]) {
                    free(local->objects[idx]);
                    atomic_fetch_add(&global_stats.free_count, 1);
                }
                
                local->objects[idx] = local->objects[local->count - 1];
                local->sizes[idx] = local->sizes[local->count - 1];
                local->count--;
            }
        }
        
        burst_count++;
        local->operations++;
    }
    
    for (size_t i = 0; i < local->count; i++) {
        if (local->objects[i]) {
            free(local->objects[i]);
            atomic_fetch_add(&global_stats.free_count, 1);
        }
    }
    
    return NULL;
}

static void run_concurrent_test(const char* name, void* (*thread_func)(void*), int duration) {
    printf("\n========================================\n");
    printf("Test: %s\n", name);
    printf("Threads: %d, Duration: %d sec\n", NUM_THREADS, duration);
    printf("========================================\n");
    
    atomic_store(&running, 1);
    atomic_store(&barrier, 0);
    memset(&global_stats, 0, sizeof(global_stats));
    
    pthread_t threads[NUM_THREADS];
    thread_local_t locals[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        memset(&locals[i], 0, sizeof(thread_local_t));
        locals[i].thread_id = i;
    }
    
    double start = get_time_ms();
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_func, &locals[i]);
    }
    
    sleep(duration);
    
    atomic_store(&running, 0);
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end = get_time_ms();
    double elapsed = (end - start) / 1000.0;
    
    long total_ops = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total_ops += locals[i].operations;
    }
    
    printf("\nResults:\n");
    printf("  Allocations:      %ld\n", atomic_load(&global_stats.alloc_count));
    printf("  Frees:            %ld\n", atomic_load(&global_stats.free_count));
    printf("  Reallocations:    %ld\n", atomic_load(&global_stats.realloc_count));
    printf("  Alloc failures:   %ld\n", atomic_load(&global_stats.alloc_failures));
    printf("  Data corruptions: %ld\n", atomic_load(&global_stats.data_corruptions));
    printf("  Race conditions:  %ld\n", atomic_load(&global_stats.race_detected));
    printf("  Total operations: %ld\n", total_ops);
    printf("  Throughput:       %.0f ops/sec\n", total_ops / elapsed);
    printf("  Elapsed time:     %.2f sec\n", elapsed);
    
    if (atomic_load(&global_stats.data_corruptions) > 0) {
        printf("\n[FAIL] Data corruption detected!\n");
    } else if (atomic_load(&global_stats.race_detected) > 0) {
        printf("\n[WARN] Alignment issues detected\n");
    } else {
        printf("\n[PASS] No data corruption\n");
    }
}

int main(void) {
    printf("\n========================================\n");
    printf("   High Concurrent Stress Test Suite\n");
    printf("========================================\n");
    
    srand(time(NULL));
    nomalloc_init();
    
    run_concurrent_test("Concurrent Allocator", concurrent_allocator_thread, 15);
    
    run_concurrent_test("Concurrent Calloc", concurrent_calloc_thread, 15);
    
    run_concurrent_test("Burst Allocator", burst_allocator_thread, 15);
    
    nomalloc_shutdown();
    
    printf("\n========================================\n");
    printf("   All Concurrent Tests Completed\n");
    printf("========================================\n");
    
    return 0;
}