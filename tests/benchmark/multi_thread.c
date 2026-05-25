#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <nomalloc/nomalloc.h>

#define NUM_THREADS 8
#define NUM_ITERATIONS 1000000
#define NUM_SIZES 10

static size_t test_sizes[NUM_SIZES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192
};

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

typedef struct {
    int thread_id;
    int iterations;
    double elapsed;
    size_t bytes_allocated;
} thread_result_t;

void* thread_func(void* arg) {
    thread_result_t* result = (thread_result_t*)arg;
    
    double start_time = get_time_sec();
    
    for (int i = 0; i < result->iterations; i++) {
        size_t size = test_sizes[i % NUM_SIZES];
        void* ptr = malloc(size);
        if (ptr) {
            memset(ptr, 0xAA, size);
            result->bytes_allocated += size;
            free(ptr);
        }
    }
    
    double end_time = get_time_sec();
    result->elapsed = end_time - start_time;
    
    return NULL;
}

void benchmark_multi_thread(void) {
    printf("Benchmark: malloc/free multi-thread\n");
    printf("Threads: %d\n", NUM_THREADS);
    printf("Iterations per thread: %d\n", NUM_ITERATIONS);
    printf("Total iterations: %d\n\n", NUM_THREADS * NUM_ITERATIONS);
    
    pthread_t threads[NUM_THREADS];
    thread_result_t results[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        results[i].thread_id = i;
        results[i].iterations = NUM_ITERATIONS;
        results[i].elapsed = 0;
        results[i].bytes_allocated = 0;
    }
    
    double start_time = get_time_sec();
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_func, &results[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end_time = get_time_sec();
    double total_elapsed = end_time - start_time;
    
    double total_ops_per_sec = (double)NUM_THREADS * NUM_ITERATIONS * 2 / total_elapsed;
    
    double min_thread_time = results[0].elapsed;
    double max_thread_time = results[0].elapsed;
    double total_thread_time = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        total_thread_time += results[i].elapsed;
        if (results[i].elapsed < min_thread_time) {
            min_thread_time = results[i].elapsed;
        }
        if (results[i].elapsed > max_thread_time) {
            max_thread_time = results[i].elapsed;
        }
    }
    
    printf("Total elapsed time: %.3f seconds\n", total_elapsed);
    printf("Total operations: %d malloc + %d free\n", 
           NUM_THREADS * NUM_ITERATIONS, NUM_THREADS * NUM_ITERATIONS);
    printf("Total throughput: %.2f M ops/sec\n", total_ops_per_sec / 1e6);
    printf("Average per-thread time: %.3f seconds\n", total_thread_time / NUM_THREADS);
    printf("Min thread time: %.3f seconds\n", min_thread_time);
    printf("Max thread time: %.3f seconds\n", max_thread_time);
    
    printf("\nPer-thread statistics:\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        double thread_ops_per_sec = (double)NUM_ITERATIONS * 2 / results[i].elapsed;
        printf("  Thread %d: %.2f M ops/sec (%.3f sec, %zu MB)\n",
               i, thread_ops_per_sec / 1e6, results[i].elapsed,
               results[i].bytes_allocated / (1024 * 1024));
    }
}

void benchmark_high_concurrency(void) {
    printf("\nBenchmark: high concurrency (16 threads)\n\n");
    
    int num_threads = 16;
    int iterations = NUM_ITERATIONS / 2;
    
    pthread_t threads[num_threads];
    thread_result_t results[num_threads];
    
    for (int i = 0; i < num_threads; i++) {
        results[i].thread_id = i;
        results[i].iterations = iterations;
        results[i].elapsed = 0;
        results[i].bytes_allocated = 0;
    }
    
    double start_time = get_time_sec();
    
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, thread_func, &results[i]);
    }
    
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end_time = get_time_sec();
    double total_elapsed = end_time - start_time;
    
    double total_ops_per_sec = (double)num_threads * iterations * 2 / total_elapsed;
    
    printf("Total throughput: %.2f M ops/sec\n", total_ops_per_sec / 1e6);
}

void print_allocator_stats(void) {
    printf("\nAllocator Statistics:\n");
    nomalloc_print_stats_summary();
}

int main(void) {
    printf("=== Nomalloc Multi-Thread Benchmark ===\n\n");
    
    nomalloc_init();
    
    benchmark_multi_thread();
    benchmark_high_concurrency();
    
    print_allocator_stats();
    
    nomalloc_shutdown();
    
    return 0;
}