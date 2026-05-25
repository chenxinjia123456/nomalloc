#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <nomalloc/nomalloc.h>

#define NUM_ITERATIONS 10000000
#define NUM_SIZES 10

static size_t test_sizes[NUM_SIZES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192
};

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void benchmark_malloc_free(void) {
    printf("Benchmark: malloc/free single-thread\n");
    printf("Iterations: %d\n\n", NUM_ITERATIONS);
    
    double start_time = get_time_sec();
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        size_t size = test_sizes[i % NUM_SIZES];
        void* ptr = malloc(size);
        if (ptr) {
            memset(ptr, 0xAA, size);
            free(ptr);
        }
    }
    
    double end_time = get_time_sec();
    double elapsed = end_time - start_time;
    double ops_per_sec = (double)NUM_ITERATIONS * 2 / elapsed;
    
    printf("Elapsed time: %.3f seconds\n", elapsed);
    printf("Operations: %d malloc + %d free\n", NUM_ITERATIONS, NUM_ITERATIONS);
    printf("Throughput: %.2f M ops/sec\n", ops_per_sec / 1e6);
    printf("Average latency: %.2f ns/op\n", 
           (elapsed * 1e9) / ((double)NUM_ITERATIONS * 2));
}

void benchmark_different_sizes(void) {
    printf("\nBenchmark: different size classes\n\n");
    
    for (int i = 0; i < NUM_SIZES; i++) {
        size_t size = test_sizes[i];
        int iterations = NUM_ITERATIONS / 10;
        
        double start_time = get_time_sec();
        
        for (int j = 0; j < iterations; j++) {
            void* ptr = malloc(size);
            if (ptr) {
                free(ptr);
            }
        }
        
        double end_time = get_time_sec();
        double elapsed = end_time - start_time;
        double ops_per_sec = (double)iterations * 2 / elapsed;
        
        printf("Size %zu bytes: %.2f M ops/sec (%.2f ns/op)\n",
               size, ops_per_sec / 1e6, (elapsed * 1e9) / ((double)iterations * 2));
    }
}

void benchmark_realloc(void) {
    printf("\nBenchmark: realloc\n\n");
    
    int iterations = NUM_ITERATIONS / 10;
    double start_time = get_time_sec();
    
    void* ptr = malloc(100);
    
    for (int i = 0; i < iterations; i++) {
        size_t new_size = 100 + (i % 100) * 10;
        ptr = realloc(ptr, new_size);
    }
    
    free(ptr);
    
    double end_time = get_time_sec();
    double elapsed = end_time - start_time;
    double ops_per_sec = (double)iterations / elapsed;
    
    printf("Realloc throughput: %.2f M ops/sec\n", ops_per_sec / 1e6);
    printf("Realloc latency: %.2f ns/op\n", (elapsed * 1e9) / iterations);
}

void benchmark_calloc(void) {
    printf("\nBenchmark: calloc\n\n");
    
    int iterations = NUM_ITERATIONS / 10;
    double start_time = get_time_sec();
    
    for (int i = 0; i < iterations; i++) {
        void* ptr = calloc(10, 100);
        if (ptr) {
            free(ptr);
        }
    }
    
    double end_time = get_time_sec();
    double elapsed = end_time - start_time;
    double ops_per_sec = (double)iterations * 2 / elapsed;
    
    printf("Calloc throughput: %.2f M ops/sec\n", ops_per_sec / 1e6);
    printf("Calloc latency: %.2f ns/op\n", (elapsed * 1e9) / iterations);
}

void print_allocator_stats(void) {
    printf("\nAllocator Statistics:\n");
    nomalloc_print_stats_summary();
}

int main(void) {
    printf("=== Nomalloc Single-Thread Benchmark ===\n\n");
    
    nomalloc_init();
    
    benchmark_malloc_free();
    benchmark_different_sizes();
    benchmark_realloc();
    benchmark_calloc();
    
    print_allocator_stats();
    
    nomalloc_shutdown();
    
    return 0;
}