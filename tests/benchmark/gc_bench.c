#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <nomalloc/nomalloc.h>
#include <nomalloc/gc_api.h>
#include <nomalloc/stats_api.h>

#define NUM_ALLOCATIONS 500000
#define MAX_SIZE 8192
#define NUM_THREADS 4

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static size_t random_size(void) {
    return (size_t)(rand() % MAX_SIZE + 1);
}

void benchmark_gc_trigger_threshold(void) {
    printf("\n=== GC Trigger Threshold Benchmark ===\n");
    
    gc_set_threshold(100 * 1024 * 1024);
    gc_set_watermark(0.8, 0.6);
    
    printf("Config: threshold=%zu bytes, watermark=(%.2f, %.2f)\n",
           gc_get_threshold(), 0.8, 0.6);
    
    double start = get_time_ms();
    
    size_t total = 0;
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        size_t size = random_size();
        void* ptr = malloc(size);
        if (ptr) {
            total += size;
            memset(ptr, 0xAA, size);
            
            if (rand() % 3 == 0) {
                free(ptr);
                total -= size;
            }
        }
    }
    
    double end = get_time_ms();
    
    printf("Allocated: %.2f MB in %.2f ms\n", total / (1024.0 * 1024.0), end - start);
    
    struct nomalloc_gc_stats gc_stats;
    gc_get_allocator_stats(&gc_stats);
    printf("GC triggered: %llu times\n", gc_stats.gc_count);
    printf("GC total time: %.2f ms\n", gc_stats.gc_time_ns / 1e6);
    printf("Bytes freed: %llu\n", gc_stats.bytes_freed);
}

void benchmark_gc_pause_time(void) {
    printf("\n=== GC Pause Time Benchmark ===\n");
    
    gc_set_pause_target(50);
    gc_set_watermark(0.7, 0.5);
    
    printf("Config: pause_target=%llu ms\n", gc_get_pause_target());
    
    void** ptrs = (void**)malloc(NUM_ALLOCATIONS * sizeof(void*));
    
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        ptrs[i] = malloc(random_size());
    }
    
    printf("Before GC: allocated %d objects\n", NUM_ALLOCATIONS);
    
    double gc_start = get_time_ms();
    gc_collect();
    double gc_end = get_time_ms();
    
    printf("GC pause: %.2f ms\n", gc_end - gc_start);
    
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        free(ptrs[i]);
    }
    
    free(ptrs);
    
    struct nomalloc_gc_stats gc_stats;
    gc_get_allocator_stats(&gc_stats);
    printf("Pause avg: %.2f ms, max: %.2f ms\n", gc_stats.pause_avg_ms, gc_stats.pause_max_ms);
}

void benchmark_gc_generations(void) {
    printf("\n=== GC Generation Benchmark ===\n");
    
    gc_set_generation_threshold(0, 32 * 1024 * 1024);
    gc_set_generation_threshold(1, 8 * 1024 * 1024);
    gc_set_generation_threshold(2, 64 * 1024 * 1024);
    
    printf("Young Gen: 32MB, Survivor: 8MB, Old Gen: 64MB\n");
    
    void** young_ptrs = (void**)malloc(100000 * sizeof(void*));
    void** old_ptrs = (void**)malloc(10000 * sizeof(void*));
    
    printf("Allocate Young Gen objects\n");
    for (int i = 0; i < 100000; i++) {
        young_ptrs[i] = malloc(64);
    }
    
    printf("Allocate Old Gen objects\n");
    for (int i = 0; i < 10000; i++) {
        old_ptrs[i] = malloc(4096);
    }
    
    printf("Free 80%% young objects (short-lived)\n");
    for (int i = 0; i < 80000; i++) {
        free(young_ptrs[i]);
        young_ptrs[i] = NULL;
    }
    
    double young_gc_start = get_time_ms();
    gc_collect_young();
    double young_gc_end = get_time_ms();
    
    printf("Young GC time: %.2f ms\n", young_gc_end - young_gc_start);
    
    printf("Free 20%% old objects\n");
    for (int i = 0; i < 2000; i++) {
        free(old_ptrs[i]);
        old_ptrs[i] = NULL;
    }
    
    double mixed_gc_start = get_time_ms();
    gc_collect_mixed();
    double mixed_gc_end = get_time_ms();
    
    printf("Mixed GC time: %.2f ms\n", mixed_gc_end - mixed_gc_start);
    
    printf("Full GC\n");
    double full_gc_start = get_time_ms();
    gc_collect_full();
    double full_gc_end = get_time_ms();
    
    printf("Full GC time: %.2f ms\n", full_gc_end - full_gc_start);
    
    for (int i = 0; i < 100000; i++) {
        if (young_ptrs[i]) free(young_ptrs[i]);
    }
    for (int i = 0; i < 10000; i++) {
        if (old_ptrs[i]) free(old_ptrs[i]);
    }
    
    free(young_ptrs);
    free(old_ptrs);
    
    struct nomalloc_gc_stats gc_stats;
    gc_get_allocator_stats(&gc_stats);
    printf("\nGC Statistics:\n");
    printf("  Young GC count: %llu\n", gc_api_get_young_gc_count());
    printf("  Mixed GC count: %llu\n", gc_api_get_mixed_gc_count());
    printf("  Full GC count: %llu\n", gc_api_get_full_gc_count());
    printf("  Total bytes freed: %llu\n", gc_stats.bytes_freed);
}

void benchmark_gc_concurrent(void) {
    printf("\n=== Concurrent GC Benchmark ===\n");
    
    gc_set_watermark(0.75, 0.55);
    
    double start = get_time_ms();
    
    printf("Start allocations with concurrent GC\n");
    
    size_t allocated = 0;
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        size_t size = random_size();
        void* ptr = malloc(size);
        if (ptr) {
            allocated += size;
            
            if (i % 10000 == 0) {
                gc_collect_async();
            }
            
            if (rand() % 4 == 0) {
                free(ptr);
                allocated -= size;
            }
        }
    }
    
    gc_collect_wait();
    
    double end = get_time_ms();
    
    printf("Total time: %.2f ms\n", end - start);
    printf("Allocated: %.2f MB\n", allocated / (1024.0 * 1024.0));
    
    struct nomalloc_gc_stats gc_stats;
    gc_get_allocator_stats(&gc_stats);
    printf("GC count: %llu, GC time: %.2f ms\n", gc_stats.gc_count, gc_stats.gc_time_ns / 1e6);
}

void benchmark_gc_throughput_impact(void) {
    printf("\n=== GC Throughput Impact Benchmark ===\n");
    
    printf("Without GC:\n");
    gc_disable();
    
    double start_no_gc = get_time_ms();
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        void* ptr = malloc(random_size());
        if (ptr) {
            memset(ptr, 0, random_size());
            if (rand() % 2 == 0) {
                free(ptr);
            }
        }
    }
    double end_no_gc = get_time_ms();
    
    printf("  Time: %.2f ms\n", end_no_gc - start_no_gc);
    printf("  Throughput: %.2f K ops/sec\n", 
           NUM_ALLOCATIONS / (end_no_gc - start_no_gc) / 1000.0);
    
    gc_enable();
    gc_set_watermark(0.7, 0.5);
    
    printf("\nWith GC:\n");
    
    double start_with_gc = get_time_ms();
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        void* ptr = malloc(random_size());
        if (ptr) {
            memset(ptr, 0, random_size());
            if (rand() % 2 == 0) {
                free(ptr);
            }
        }
    }
    double end_with_gc = get_time_ms();
    
    printf("  Time: %.2f ms\n", end_with_gc - start_with_gc);
    printf("  Throughput: %.2f K ops/sec\n",
           NUM_ALLOCATIONS / (end_with_gc - start_with_gc) / 1000.0);
    
    struct nomalloc_gc_stats gc_stats;
    gc_get_allocator_stats(&gc_stats);
    printf("  GC overhead: %.2f%%\n", 
           gc_stats.throughput_impact_pct * 100.0);
}

struct thread_data {
    int thread_id;
    int iterations;
    double elapsed_ms;
};

static void* allocation_thread(void* arg) {
    struct thread_data* data = (struct thread_data*)arg;
    
    double start = get_time_ms();
    
    for (int i = 0; i < data->iterations; i++) {
        void* ptr = malloc(random_size());
        if (ptr) {
            memset(ptr, 0xAA, random_size());
            if (rand() % 3 == 0) {
                free(ptr);
            }
        }
    }
    
    double end = get_time_ms();
    data->elapsed_ms = end - start;
    
    return NULL;
}

void benchmark_gc_multithread(void) {
    printf("\n=== GC Multi-thread Benchmark ===\n");
    
    gc_set_watermark(0.75, 0.55);
    
    pthread_t threads[NUM_THREADS];
    struct thread_data data[NUM_THREADS];
    
    printf("Running %d threads with %d iterations each\n", NUM_THREADS, NUM_ALLOCATIONS / NUM_THREADS);
    
    double start = get_time_ms();
    
    for (int i = 0; i < NUM_THREADS; i++) {
        data[i].thread_id = i;
        data[i].iterations = NUM_ALLOCATIONS / NUM_THREADS;
        pthread_create(&threads[i], NULL, allocation_thread, &data[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end = get_time_ms();
    
    double total_elapsed = 0.0;
    for (int i = 0; i < NUM_THREADS; i++) {
        total_elapsed += data[i].elapsed_ms;
    }
    
    printf("Total time: %.2f ms\n", end - start);
    printf("Avg thread time: %.2f ms\n", total_elapsed / NUM_THREADS);
    printf("Combined throughput: %.2f K ops/sec\n",
           NUM_ALLOCATIONS / (end - start) / 1000.0);
    
    struct nomalloc_gc_stats gc_stats;
    gc_get_allocator_stats(&gc_stats);
    printf("GC count: %llu, Pause avg: %.2f ms\n", 
           gc_stats.gc_count, gc_stats.pause_avg_ms);
}

int main(void) {
    printf("=== Nomalloc GC Benchmark ===\n\n");
    
    srand(42);
    
    nomalloc_init();
    stats_enable();
    gc_enable();
    
    benchmark_gc_trigger_threshold();
    stats_reset();
    
    benchmark_gc_pause_time();
    stats_reset();
    
    benchmark_gc_generations();
    stats_reset();
    
    benchmark_gc_concurrent();
    stats_reset();
    
    benchmark_gc_throughput_impact();
    stats_reset();
    
    benchmark_gc_multithread();
    
    printf("\n=== Final Statistics ===\n");
    stats_print_summary();
    gc_print_stats();
    
    nomalloc_shutdown();
    
    return 0;
}