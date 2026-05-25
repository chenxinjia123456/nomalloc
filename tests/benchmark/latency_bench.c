#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <nomalloc/nomalloc.h>
#include <nomalloc/stats_api.h>

#define NUM_ITERATIONS 100000
#define NUM_SIZES 10
#define LATENCY_BUCKET_SIZE 16
#define MAX_LATENCY_BUCKETS 64

static size_t test_sizes[NUM_SIZES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192
};

static double get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

struct latency_histogram {
    uint64_t buckets[MAX_LATENCY_BUCKETS];
    uint64_t total_samples;
    uint64_t sum_ns;
    uint64_t min_ns;
    uint64_t max_ns;
};

static void latency_histogram_init(struct latency_histogram* hist) {
    memset(hist, 0, sizeof(struct latency_histogram));
    hist->min_ns = UINT64_MAX;
    hist->max_ns = 0;
}

static void latency_histogram_add(struct latency_histogram* hist, uint64_t value) {
    size_t bucket_idx = value / LATENCY_BUCKET_SIZE;
    if (bucket_idx >= MAX_LATENCY_BUCKETS) {
        bucket_idx = MAX_LATENCY_BUCKETS - 1;
    }
    
    hist->buckets[bucket_idx]++;
    hist->total_samples++;
    hist->sum_ns += value;
    
    if (value < hist->min_ns) {
        hist->min_ns = value;
    }
    if (value > hist->max_ns) {
        hist->max_ns = value;
    }
}

static uint64_t latency_histogram_get_percentile(struct latency_histogram* hist, double percentile) {
    if (hist->total_samples == 0) return 0;
    
    uint64_t target = (uint64_t)(hist->total_samples * percentile / 100.0);
    uint64_t cumulative = 0;
    
    for (size_t i = 0; i < MAX_LATENCY_BUCKETS; i++) {
        cumulative += hist->buckets[i];
        if (cumulative >= target) {
            return i * LATENCY_BUCKET_SIZE;
        }
    }
    
    return hist->max_ns;
}

static void latency_histogram_print(struct latency_histogram* hist, const char* name) {
    printf("\n%s Latency Distribution:\n", name);
    printf("  Samples: %llu\n", hist->total_samples);
    printf("  Min: %llu ns\n", hist->min_ns);
    printf("  Max: %llu ns\n", hist->max_ns);
    printf("  Avg: %.2f ns\n", (double)hist->sum_ns / hist->total_samples);
    printf("  P50: %llu ns\n", latency_histogram_get_percentile(hist, 50.0));
    printf("  P90: %llu ns\n", latency_histogram_get_percentile(hist, 90.0));
    printf("  P95: %llu ns\n", latency_histogram_get_percentile(hist, 95.0));
    printf("  P99: %llu ns\n", latency_histogram_get_percentile(hist, 99.0));
    printf("  P99.9: %llu ns\n", latency_histogram_get_percentile(hist, 99.9));
    
    printf("\n  Distribution:\n");
    uint64_t cumulative = 0;
    for (size_t i = 0; i < MAX_LATENCY_BUCKETS; i++) {
        if (hist->buckets[i] > 0) {
            cumulative += hist->buckets[i];
            printf("    [%zu-%zu ns]: %llu (%.2f%%, cum: %.2f%%)\n",
                   i * LATENCY_BUCKET_SIZE, (i + 1) * LATENCY_BUCKET_SIZE - 1,
                   hist->buckets[i],
                   (double)hist->buckets[i] / hist->total_samples * 100.0,
                   (double)cumulative / hist->total_samples * 100.0);
        }
    }
}

void benchmark_alloc_latency(void) {
    printf("\n=== Allocation Latency Benchmark ===\n");
    
    struct latency_histogram alloc_hist;
    latency_histogram_init(&alloc_hist);
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        size_t size = test_sizes[i % NUM_SIZES];
        
        double start = get_time_ns();
        void* ptr = malloc(size);
        double end = get_time_ns();
        
        if (ptr) {
            latency_histogram_add(&alloc_hist, (uint64_t)(end - start));
            free(ptr);
        }
    }
    
    latency_histogram_print(&alloc_hist, "malloc");
}

void benchmark_free_latency(void) {
    printf("\n=== Free Latency Benchmark ===\n");
    
    struct latency_histogram free_hist;
    latency_histogram_init(&free_hist);
    
    void** ptrs = (void**)malloc(NUM_ITERATIONS * sizeof(void*));
    size_t* sizes = (size_t*)malloc(NUM_ITERATIONS * sizeof(size_t));
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        sizes[i] = test_sizes[i % NUM_SIZES];
        ptrs[i] = malloc(sizes[i]);
    }
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        if (ptrs[i]) {
            double start = get_time_ns();
            free(ptrs[i]);
            double end = get_time_ns();
            
            latency_histogram_add(&free_hist, (uint64_t)(end - start));
        }
    }
    
    latency_histogram_print(&free_hist, "free");
    
    free(ptrs);
    free(sizes);
}

void benchmark_size_class_latency(void) {
    printf("\n=== Size Class Latency Benchmark ===\n");
    
    for (int s = 0; s < NUM_SIZES; s++) {
        size_t size = test_sizes[s];
        struct latency_histogram hist;
        latency_histogram_init(&hist);
        
        for (int i = 0; i < NUM_ITERATIONS / NUM_SIZES; i++) {
            double start = get_time_ns();
            void* ptr = malloc(size);
            double alloc_end = get_time_ns();
            
            if (ptr) {
                memset(ptr, 0xAA, size);
                double free_start = get_time_ns();
                free(ptr);
                double free_end = get_time_ns();
                
                latency_histogram_add(&hist, (uint64_t)(alloc_end - start));
            }
        }
        
        printf("\nSize %zu bytes:\n", size);
        printf("  Avg latency: %.2f ns\n", (double)hist.sum_ns / hist.total_samples);
        printf("  P50: %llu ns, P99: %llu ns, Max: %llu ns\n",
               latency_histogram_get_percentile(&hist, 50.0),
               latency_histogram_get_percentile(&hist, 99.0),
               hist.max_ns);
    }
}

void benchmark_realloc_latency(void) {
    printf("\n=== Realloc Latency Benchmark ===\n");
    
    struct latency_histogram realloc_hist;
    latency_histogram_init(&realloc_hist);
    
    void* ptr = malloc(100);
    
    for (int i = 0; i < NUM_ITERATIONS / 10; i++) {
        size_t new_size = 100 + (i % 100) * 10;
        
        double start = get_time_ns();
        ptr = realloc(ptr, new_size);
        double end = get_time_ns();
        
        if (ptr) {
            latency_histogram_add(&realloc_hist, (uint64_t)(end - start));
        }
    }
    
    free(ptr);
    
    latency_histogram_print(&realloc_hist, "realloc");
}

void benchmark_calloc_latency(void) {
    printf("\n=== Calloc Latency Benchmark ===\n");
    
    struct latency_histogram calloc_hist;
    latency_histogram_init(&calloc_hist);
    
    for (int i = 0; i < NUM_ITERATIONS / 10; i++) {
        double start = get_time_ns();
        void* ptr = calloc(10, 100);
        double end = get_time_ns();
        
        if (ptr) {
            latency_histogram_add(&calloc_hist, (uint64_t)(end - start));
            free(ptr);
        }
    }
    
    latency_histogram_print(&calloc_hist, "calloc");
}

void benchmark_aligned_alloc_latency(void) {
    printf("\n=== Aligned Alloc Latency Benchmark ===\n");
    
    struct latency_histogram aligned_hist;
    latency_histogram_init(&aligned_hist);
    
    size_t alignments[] = {64, 128, 256, 512, 1024, 4096};
    int num_alignments = 6;
    
    for (int a = 0; a < num_alignments; a++) {
        size_t alignment = alignments[a];
        latency_histogram_init(&aligned_hist);
        
        for (int i = 0; i < NUM_ITERATIONS / num_alignments; i++) {
            size_t size = test_sizes[i % NUM_SIZES];
            
            double start = get_time_ns();
            void* ptr = aligned_alloc(alignment, size);
            double end = get_time_ns();
            
            if (ptr) {
                latency_histogram_add(&aligned_hist, (uint64_t)(end - start));
                free(ptr);
            }
        }
        
        printf("\nAlignment %zu bytes:\n", alignment);
        printf("  Avg latency: %.2f ns\n", (double)aligned_hist.sum_ns / aligned_hist.total_samples);
        printf("  P99: %llu ns, Max: %llu ns\n",
               latency_histogram_get_percentile(&aligned_hist, 99.0),
               aligned_hist.max_ns);
    }
}

void print_summary(void) {
    printf("\n=== Allocator Summary ===\n");
    
    uint64_t min, max, avg, p50, p99;
    stats_get_latency_stats(&min, &max, &avg, &p50, &p99);
    
    printf("  Total allocated: %llu bytes\n", stats_get_total_allocated());
    printf("  Total freed: %llu bytes\n", stats_get_total_freed());
    printf("  Active: %llu bytes\n", stats_get_active_allocations());
    printf("  Alloc ops: %llu\n", stats_get_alloc_ops());
    printf("  Free ops: %llu\n", stats_get_free_ops());
    printf("  Latency (min/avg/p50/p99/max): %llu/%llu/%llu/%llu/%llu ns\n",
           min, avg, p50, p99, max);
}

int main(void) {
    printf("=== Nomalloc Latency Benchmark ===\n\n");
    
    nomalloc_init();
    stats_enable();
    
    printf("Configuration:\n");
    printf("  Iterations: %d\n", NUM_ITERATIONS);
    printf("  Size classes: %d\n", NUM_SIZES);
    
    benchmark_alloc_latency();
    benchmark_free_latency();
    benchmark_size_class_latency();
    benchmark_realloc_latency();
    benchmark_calloc_latency();
    benchmark_aligned_alloc_latency();
    
    print_summary();
    
    stats_print_summary();
    
    nomalloc_shutdown();
    
    return 0;
}