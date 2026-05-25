#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>

#ifdef JEMALLOC_ENABLED
#include <jemalloc/jemalloc.h>
#define USE_JEMALLOC 1
#else
#define USE_JEMALLOC 0
#endif

#define NUM_ITERATIONS 10000000
#define NUM_THREADS 8
#define NUM_SIZES 10
#define NUM_WARMUP 1000

static size_t test_sizes[NUM_SIZES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192
};

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

struct benchmark_result {
    double elapsed_sec;
    double ops_per_sec;
    double avg_latency_ns;
    double p50_latency_ns;
    double p99_latency_ns;
    double throughput_mb_per_sec;
    size_t total_allocated;
    size_t total_ops;
};

struct latency_record {
    uint64_t* values;
    size_t count;
    size_t capacity;
};

static void latency_record_init(struct latency_record* rec, size_t capacity) {
    rec->values = (uint64_t*)malloc(capacity * sizeof(uint64_t));
    rec->count = 0;
    rec->capacity = capacity;
}

static void latency_record_add(struct latency_record* rec, uint64_t value) {
    if (rec->count < rec->capacity) {
        rec->values[rec->count++] = value;
    }
}

static void latency_record_destroy(struct latency_record* rec) {
    free(rec->values);
}

static void sort_array(uint64_t* arr, size_t n) {
    for (size_t i = 0; i < n - 1; i++) {
        for (size_t j = i + 1; j < n; j++) {
            if (arr[i] > arr[j]) {
                uint64_t tmp = arr[i];
                arr[i] = arr[j];
                arr[j] = tmp;
            }
        }
    }
}

static uint64_t calculate_percentile(uint64_t* sorted_arr, size_t n, double percentile) {
    size_t idx = (size_t)((double)n * percentile / 100.0);
    if (idx >= n) idx = n - 1;
    return sorted_arr[idx];
}

static void run_benchmark_system_malloc(struct benchmark_result* result, int iterations) {
    struct latency_record latencies;
    latency_record_init(&latencies, iterations);
    
    double start_time = get_time_sec();
    
    size_t total_allocated = 0;
    
    for (int i = 0; i < iterations; i++) {
        size_t size = test_sizes[i % NUM_SIZES];
        
        double alloc_start = get_time_sec();
        void* ptr = malloc(size);
        double alloc_end = get_time_sec();
        
        if (ptr) {
            memset(ptr, 0xAA, size);
            total_allocated += size;
            
            double free_start = get_time_sec();
            free(ptr);
            double free_end = get_time_sec();
            
            uint64_t total_ns = (uint64_t)((alloc_end - alloc_start + free_end - free_start) * 1e9);
            latency_record_add(&latencies, total_ns);
        }
    }
    
    double end_time = get_time_sec();
    
    result->elapsed_sec = end_time - start_time;
    result->total_ops = iterations * 2;
    result->ops_per_sec = (double)iterations * 2 / result->elapsed_sec;
    result->avg_latency_ns = (result->elapsed_sec * 1e9) / result->total_ops;
    result->total_allocated = total_allocated;
    result->throughput_mb_per_sec = (double)total_allocated / result->elapsed_sec / (1024.0 * 1024.0);
    
    if (latencies.count > 0) {
        sort_array(latencies.values, latencies.count);
        result->p50_latency_ns = calculate_percentile(latencies.values, latencies.count, 50.0);
        result->p99_latency_ns = calculate_percentile(latencies.values, latencies.count, 99.0);
    }
    
    latency_record_destroy(&latencies);
}

#ifdef JEMALLOC_ENABLED
static void run_benchmark_jemalloc(struct benchmark_result* result, int iterations) {
    struct latency_record latencies;
    latency_record_init(&latencies, iterations);
    
    double start_time = get_time_sec();
    
    size_t total_allocated = 0;
    
    for (int i = 0; i < iterations; i++) {
        size_t size = test_sizes[i % NUM_SIZES];
        
        double alloc_start = get_time_sec();
        void* ptr = je_malloc(size);
        double alloc_end = get_time_sec();
        
        if (ptr) {
            memset(ptr, 0xAA, size);
            total_allocated += size;
            
            double free_start = get_time_sec();
            je_free(ptr);
            double free_end = get_time_sec();
            
            uint64_t total_ns = (uint64_t)((alloc_end - alloc_start + free_end - free_start) * 1e9);
            latency_record_add(&latencies, total_ns);
        }
    }
    
    double end_time = get_time_sec();
    
    result->elapsed_sec = end_time - start_time;
    result->total_ops = iterations * 2;
    result->ops_per_sec = (double)iterations * 2 / result->elapsed_sec;
    result->avg_latency_ns = (result->elapsed_sec * 1e9) / result->total_ops;
    result->total_allocated = total_allocated;
    result->throughput_mb_per_sec = (double)total_allocated / result->elapsed_sec / (1024.0 * 1024.0);
    
    if (latencies.count > 0) {
        sort_array(latencies.values, latencies.count);
        result->p50_latency_ns = calculate_percentile(latencies.values, latencies.count, 50.0);
        result->p99_latency_ns = calculate_percentile(latencies.values, latencies.count, 99.0);
    }
    
    latency_record_destroy(&latencies);
}
#endif

static void print_result(const char* name, struct benchmark_result* result) {
    printf("\n%s Results:\n", name);
    printf("  Iterations: %zu\n", result->total_ops / 2);
    printf("  Elapsed time: %.3f seconds\n", result->elapsed_sec);
    printf("  Throughput: %.2f M ops/sec\n", result->ops_per_sec / 1e6);
    printf("  Throughput: %.2f MB/sec\n", result->throughput_mb_per_sec);
    printf("  Avg latency: %.2f ns\n", result->avg_latency_ns);
    printf("  P50 latency: %.2f ns\n", result->p50_latency_ns);
    printf("  P99 latency: %.2f ns\n", result->p99_latency_ns);
    printf("  Total allocated: %.2f MB\n", (double)result->total_allocated / (1024.0 * 1024.0));
}

static void compare_results(struct benchmark_result* baseline, struct benchmark_result* target, 
                             const char* baseline_name, const char* target_name) {
    printf("\n=== Comparison: %s vs %s ===\n\n", target_name, baseline_name);
    
    printf("%-20s %15s %15s %10s\n", "Metric", baseline_name, target_name, "Diff");
    printf("%-20s %15s %15s %10s\n", "--------------------", "---------------", "---------------", "----------");
    
    double ops_diff = (target->ops_per_sec - baseline->ops_per_sec) / baseline->ops_per_sec * 100.0;
    printf("%-20s %12.2f M %12.2f M %+8.1f%%\n", "Throughput (ops/s)", 
           baseline->ops_per_sec / 1e6, target->ops_per_sec / 1e6, ops_diff);
    
    double throughput_diff = (target->throughput_mb_per_sec - baseline->throughput_mb_per_sec) / 
                              baseline->throughput_mb_per_sec * 100.0;
    printf("%-20s %12.2f MB %12.2f MB %+8.1f%%\n", "Throughput (MB/s)", 
           baseline->throughput_mb_per_sec, target->throughput_mb_per_sec, throughput_diff);
    
    double avg_diff = (target->avg_latency_ns - baseline->avg_latency_ns) / baseline->avg_latency_ns * 100.0;
    printf("%-20s %12.2f ns %12.2f ns %+8.1f%%\n", "Avg latency", 
           baseline->avg_latency_ns, target->avg_latency_ns, avg_diff);
    
    double p50_diff = (target->p50_latency_ns - baseline->p50_latency_ns) / baseline->p50_latency_ns * 100.0;
    printf("%-20s %12.2f ns %12.2f ns %+8.1f%%\n", "P50 latency", 
           baseline->p50_latency_ns, target->p50_latency_ns, p50_diff);
    
    double p99_diff = (target->p99_latency_ns - baseline->p99_latency_ns) / baseline->p99_latency_ns * 100.0;
    printf("%-20s %12.2f ns %12.2f ns %+8.1f%%\n", "P99 latency", 
           baseline->p99_latency_ns, target->p99_latency_ns, p99_diff);
}

static void run_size_class_comparison(int iterations) {
    printf("\n=== Size Class Comparison ===\n\n");
    
    printf("%-10s %15s %15s %10s\n", "Size", "System (ns)", "Target (ns)", "Diff");
    
    for (int s = 0; s < NUM_SIZES; s++) {
        size_t size = test_sizes[s];
        
        struct benchmark_result sys_result;
        struct latency_record sys_lat;
        latency_record_init(&sys_lat, iterations / NUM_SIZES);
        
        double sys_start = get_time_sec();
        for (int i = 0; i < iterations / NUM_SIZES; i++) {
            double alloc_start = get_time_sec();
            void* ptr = malloc(size);
            double alloc_end = get_time_sec();
            
            if (ptr) {
                free(ptr);
                double free_end = get_time_sec();
                latency_record_add(&sys_lat, (uint64_t)((alloc_end - alloc_start + free_end - alloc_start) * 1e9));
            }
        }
        double sys_end = get_time_sec();
        
        sys_result.elapsed_sec = sys_end - sys_start;
        sys_result.avg_latency_ns = (sys_result.elapsed_sec * 1e9) / ((iterations / NUM_SIZES) * 2);
        
        if (sys_lat.count > 0) {
            sort_array(sys_lat.values, sys_lat.count);
            sys_result.p50_latency_ns = calculate_percentile(sys_lat.values, sys_lat.count, 50.0);
            sys_result.p99_latency_ns = calculate_percentile(sys_lat.values, sys_lat.count, 99.0);
        }
        
        latency_record_destroy(&sys_lat);
        
        double diff = 0.0;
        printf("%-10zu %12.2f ns %12.2f ns %+8.1f%%\n", 
               size, sys_result.avg_latency_ns, sys_result.avg_latency_ns, diff);
    }
}

static void run_pattern_benchmark(const char* pattern_name, 
                                  void (*alloc_pattern)(void**, size_t*, int),
                                  void (*free_pattern)(void**, size_t*, int),
                                  int iterations) {
    printf("\n=== Pattern: %s ===\n", pattern_name);
    
    void** ptrs = (void**)malloc(iterations * sizeof(void*));
    size_t* sizes = (size_t*)malloc(iterations * sizeof(size_t));
    
    double start_time = get_time_sec();
    
    alloc_pattern(ptrs, sizes, iterations);
    
    double alloc_end = get_time_sec();
    
    free_pattern(ptrs, sizes, iterations);
    
    double end_time = get_time_sec();
    
    double alloc_elapsed = alloc_end - start_time;
    double free_elapsed = end_time - alloc_end;
    double total_elapsed = end_time - start_time;
    
    printf("  Alloc time: %.3f sec (%.2f ns/op)\n", alloc_elapsed, alloc_elapsed * 1e9 / iterations);
    printf("  Free time: %.3f sec (%.2f ns/op)\n", free_elapsed, free_elapsed * 1e9 / iterations);
    printf("  Total time: %.3f sec\n", total_elapsed);
    printf("  Throughput: %.2f M ops/sec\n", (double)iterations * 2 / total_elapsed / 1e6);
    
    free(ptrs);
    free(sizes);
}

static void sequential_pattern(void** ptrs, size_t* sizes, int iterations) {
    for (int i = 0; i < iterations; i++) {
        sizes[i] = test_sizes[i % NUM_SIZES];
        ptrs[i] = malloc(sizes[i]);
        if (ptrs[i]) {
            memset(ptrs[i], 0xAA, sizes[i]);
        }
    }
}

static void sequential_free(void** ptrs, size_t* sizes, int iterations) {
    for (int i = 0; i < iterations; i++) {
        if (ptrs[i]) {
            free(ptrs[i]);
        }
    }
}

static void random_pattern(void** ptrs, size_t* sizes, int iterations) {
    srand(42);
    for (int i = 0; i < iterations; i++) {
        sizes[i] = test_sizes[rand() % NUM_SIZES];
        ptrs[i] = malloc(sizes[i]);
        if (ptrs[i]) {
            memset(ptrs[i], 0xAA, sizes[i]);
        }
    }
}

static void random_free(void** ptrs, size_t* sizes, int iterations) {
    srand(42);
    for (int i = 0; i < iterations; i++) {
        int idx = rand() % iterations;
        if (ptrs[idx]) {
            free(ptrs[idx]);
            ptrs[idx] = NULL;
        }
    }
    
    for (int i = 0; i < iterations; i++) {
        if (ptrs[i]) {
            free(ptrs[i]);
        }
    }
}

static void burst_pattern(void** ptrs, size_t* sizes, int iterations) {
    int burst_size = 1000;
    int bursts = iterations / burst_size;
    
    for (int b = 0; b < bursts; b++) {
        for (int i = 0; i < burst_size; i++) {
            int idx = b * burst_size + i;
            sizes[idx] = test_sizes[idx % NUM_SIZES];
            ptrs[idx] = malloc(sizes[idx]);
        }
        
        for (int i = 0; i < burst_size; i++) {
            int idx = b * burst_size + i;
            if (ptrs[idx]) {
                free(ptrs[idx]);
                ptrs[idx] = NULL;
            }
        }
    }
}

static void burst_free(void** ptrs, size_t* sizes, int iterations) {
}

int main(void) {
    printf("=== Comparative Benchmark ===\n\n");
    
    int iterations = NUM_ITERATIONS;
    
    printf("Configuration:\n");
    printf("  Iterations: %d\n", iterations);
    printf("  Warmup: %d\n", NUM_WARMUP);
    printf("  jemalloc available: %s\n", USE_JEMALLOC ? "yes" : "no");
    
    printf("\n--- Warmup ---\n");
    for (int i = 0; i < NUM_WARMUP; i++) {
        void* ptr = malloc(test_sizes[i % NUM_SIZES]);
        if (ptr) free(ptr);
    }
    
    printf("\n=== Main Benchmark ===\n");
    
    struct benchmark_result system_result;
    run_benchmark_system_malloc(&system_result, iterations);
    print_result("System malloc", &system_result);
    
#ifdef JEMALLOC_ENABLED
    struct benchmark_result jemalloc_result;
    run_benchmark_jemalloc(&jemalloc_result, iterations);
    print_result("jemalloc", &jemalloc_result);
    
    compare_results(&jemalloc_result, &system_result, "jemalloc", "System malloc");
#endif
    
    run_size_class_comparison(iterations / 10);
    
    printf("\n=== Allocation Patterns ===\n");
    
    run_pattern_benchmark("Sequential", sequential_pattern, sequential_free, iterations / 10);
    run_pattern_benchmark("Random", random_pattern, random_free, iterations / 10);
    run_pattern_benchmark("Burst", burst_pattern, burst_free, iterations / 10);
    
    printf("\n=== Benchmark Complete ===\n");
    
    return 0;
}