#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <nomalloc/nomalloc.h>

#define NUM_THREADS 8
#define TEST_DURATION_SEC 60
#define MAX_ALLOCATIONS 10000

static volatile int running = 1;

typedef struct {
    int thread_id;
    size_t total_allocs;
    size_t total_frees;
    size_t current_active;
    size_t max_active;
} thread_stats_t;

static thread_stats_t thread_stats[NUM_THREADS];

void* stress_thread_func(void* arg) {
    int thread_id = *((int*)arg);
    thread_stats[thread_id].thread_id = thread_id;
    thread_stats[thread_id].total_allocs = 0;
    thread_stats[thread_id].total_frees = 0;
    thread_stats[thread_id].current_active = 0;
    thread_stats[thread_id].max_active = 0;
    
    void* allocations[MAX_ALLOCATIONS];
    size_t alloc_sizes[MAX_ALLOCATIONS];
    size_t alloc_count = 0;
    
    while (running) {
        if (rand() % 2 == 0 && alloc_count < MAX_ALLOCATIONS) {
            size_t size = (rand() % 1000 + 1) * 8;
            void* ptr = malloc(size);
            if (ptr) {
                memset(ptr, 0xAA, size);
                allocations[alloc_count] = ptr;
                alloc_sizes[alloc_count] = size;
                alloc_count++;
                
                thread_stats[thread_id].total_allocs++;
                thread_stats[thread_id].current_active++;
                
                if (thread_stats[thread_id].current_active > 
                    thread_stats[thread_id].max_active) {
                    thread_stats[thread_id].max_active = 
                        thread_stats[thread_id].current_active;
                }
            }
        } else if (alloc_count > 0) {
            size_t idx = rand() % alloc_count;
            free(allocations[idx]);
            
            allocations[idx] = allocations[alloc_count - 1];
            alloc_sizes[idx] = alloc_sizes[alloc_count - 1];
            alloc_count--;
            
            thread_stats[thread_id].total_frees++;
            thread_stats[thread_id].current_active--;
        }
        
        usleep(rand() % 100);
    }
    
    for (size_t i = 0; i < alloc_count; i++) {
        free(allocations[i]);
        thread_stats[thread_id].total_frees++;
    }
    
    return NULL;
}

void print_stats(void) {
    printf("\n=== Stress Test Statistics ===\n\n");
    
    size_t total_allocs = 0;
    size_t total_frees = 0;
    size_t total_active = 0;
    size_t total_max = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        printf("Thread %d:\n", i);
        printf("  Total allocations: %zu\n", thread_stats[i].total_allocs);
        printf("  Total frees:       %zu\n", thread_stats[i].total_frees);
        printf("  Current active:    %zu\n", thread_stats[i].current_active);
        printf("  Max active:        %zu\n", thread_stats[i].max_active);
        printf("  Throughput:        %.2f ops/sec\n", 
               (double)(thread_stats[i].total_allocs + thread_stats[i].total_frees) / 
               TEST_DURATION_SEC);
        
        total_allocs += thread_stats[i].total_allocs;
        total_frees += thread_stats[i].total_frees;
        total_active += thread_stats[i].current_active;
        total_max += thread_stats[i].max_active;
    }
    
    printf("\nTotal:\n");
    printf("  Total allocations: %zu\n", total_allocs);
    printf("  Total frees:       %zu\n", total_frees);
    printf("  Total throughput:  %.2f ops/sec\n",
           (double)(total_allocs + total_frees) / TEST_DURATION_SEC);
    
    printf("\nAllocator Statistics:\n");
    nomalloc_print_stats_summary();
}

int main(void) {
    printf("=== Nomalloc Long Running Stress Test ===\n");
    printf("Duration: %d seconds\n", TEST_DURATION_SEC);
    printf("Threads: %d\n\n", NUM_THREADS);
    
    nomalloc_init();
    
    srand(time(NULL));
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, stress_thread_func, &thread_ids[i]);
    }
    
    printf("Running stress test for %d seconds...\n", TEST_DURATION_SEC);
    
    sleep(TEST_DURATION_SEC);
    
    running = 0;
    
    printf("Stopping threads...\n");
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    print_stats();
    
    nomalloc_shutdown();
    
    printf("\nStress test completed successfully!\n");
    return 0;
}