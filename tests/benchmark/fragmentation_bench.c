#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <nomalloc/nomalloc.h>
#include <nomalloc/stats_api.h>
#include <nomalloc/gc_api.h>

#define NUM_ALLOCATIONS 100000
#define MAX_ALLOCATION_SIZE 8192
#define FRAGMENTATION_CYCLES 10

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

struct allocation_info {
    void* ptr;
    size_t size;
    int allocated;
};

static size_t random_size(void) {
    return (size_t)(rand() % MAX_ALLOCATION_SIZE + 1);
}

void benchmark_fragmentation_pattern1(void) {
    printf("\n=== Fragmentation Pattern 1: Random Allocate/Free ===\n");
    
    struct allocation_info* allocs = (struct allocation_info*)malloc(
        NUM_ALLOCATIONS * sizeof(struct allocation_info));
    
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        allocs[i].ptr = NULL;
        allocs[i].size = 0;
        allocs[i].allocated = 0;
    }
    
    printf("Phase 1: Allocate all\n");
    size_t total_allocated = 0;
    
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        allocs[i].size = random_size();
        allocs[i].ptr = malloc(allocs[i].size);
        if (allocs[i].ptr) {
            allocs[i].allocated = 1;
            total_allocated += allocs[i].size;
            memset(allocs[i].ptr, 0xAA, allocs[i].size);
        }
    }
    
    printf("  Allocated: %zu objects, %zu bytes (%.2f MB)\n",
           NUM_ALLOCATIONS, total_allocated, total_allocated / (1024.0 * 1024.0));
    
    double fragmentation = stats_get_fragmentation_ratio();
    double utilization = stats_get_memory_utilization();
    printf("  Fragmentation ratio: %.2f%%\n", fragmentation * 100.0);
    printf("  Memory utilization: %.2f%%\n", utilization * 100.0);
    
    printf("\nPhase 2: Free 50%% random\n");
    int freed_count = 0;
    size_t freed_bytes = 0;
    
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        if (rand() % 2 == 0 && allocs[i].allocated) {
            free(allocs[i].ptr);
            allocs[i].allocated = 0;
            freed_bytes += allocs[i].size;
            freed_count++;
        }
    }
    
    printf("  Freed: %d objects, %zu bytes\n", freed_count, freed_bytes);
    
    fragmentation = stats_get_fragmentation_ratio();
    utilization = stats_get_memory_utilization();
    printf("  Fragmentation ratio: %.2f%%\n", fragmentation * 100.0);
    printf("  Memory utilization: %.2f%%\n", utilization * 100.0);
    
    printf("\nPhase 3: Reallocate freed slots\n");
    size_t reallocated = 0;
    
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        if (!allocs[i].allocated) {
            allocs[i].size = random_size();
            allocs[i].ptr = malloc(allocs[i].size);
            if (allocs[i].ptr) {
                allocs[i].allocated = 1;
                reallocated += allocs[i].size;
            }
        }
    }
    
    printf("  Reallocated: %zu bytes\n", reallocated);
    
    fragmentation = stats_get_fragmentation_ratio();
    utilization = stats_get_memory_utilization();
    printf("  Fragmentation ratio: %.2f%%\n", fragmentation * 100.0);
    printf("  Memory utilization: %.2f%%\n", utilization * 100.0);
    
    printf("\nPhase 4: Cleanup\n");
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        if (allocs[i].allocated) {
            free(allocs[i].ptr);
        }
    }
    
    fragmentation = stats_get_fragmentation_ratio();
    utilization = stats_get_memory_utilization();
    printf("  Fragmentation ratio after cleanup: %.2f%%\n", fragmentation * 100.0);
    printf("  Memory utilization after cleanup: %.2f%%\n", utilization * 100.0);
    
    free(allocs);
}

void benchmark_fragmentation_pattern2(void) {
    printf("\n=== Fragmentation Pattern 2: Size Gradient ===\n");
    
    struct allocation_info* allocs = (struct allocation_info*)malloc(
        NUM_ALLOCATIONS * sizeof(struct allocation_info));
    
    printf("Allocate increasing sizes, then free decreasing\n");
    
    size_t total = 0;
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        allocs[i].size = (size_t)(i + 1) * 8;
        if (allocs[i].size > MAX_ALLOCATION_SIZE) {
            allocs[i].size = MAX_ALLOCATION_SIZE;
        }
        allocs[i].ptr = malloc(allocs[i].size);
        if (allocs[i].ptr) {
            allocs[i].allocated = 1;
            total += allocs[i].size;
        }
    }
    
    printf("  Allocated: %zu bytes\n", total);
    printf("  Fragmentation: %.2f%%\n", stats_get_fragmentation_ratio() * 100.0);
    
    for (int i = NUM_ALLOCATIONS - 1; i >= NUM_ALLOCATIONS / 2; i--) {
        if (allocs[i].allocated) {
            free(allocs[i].ptr);
            allocs[i].allocated = 0;
        }
    }
    
    printf("  After freeing largest 50%%:\n");
    printf("    Fragmentation: %.2f%%\n", stats_get_fragmentation_ratio() * 100.0);
    printf("    Utilization: %.2f%%\n", stats_get_memory_utilization() * 100.0);
    
    for (int i = 0; i < NUM_ALLOCATIONS / 2; i++) {
        if (allocs[i].allocated) {
            free(allocs[i].ptr);
            allocs[i].allocated = 0;
        }
    }
    
    printf("  After freeing remaining:\n");
    printf("    Fragmentation: %.2f%%\n", stats_get_fragmentation_ratio() * 100.0);
    
    free(allocs);
}

void benchmark_fragmentation_pattern3(void) {
    printf("\n=== Fragmentation Pattern 3: Intermixed Sizes ===\n");
    
    void** small_ptrs = (void**)malloc(NUM_ALLOCATIONS / 2 * sizeof(void*));
    void** large_ptrs = (void**)malloc(NUM_ALLOCATIONS / 2 * sizeof(void*));
    
    printf("Allocate small and large intermixed\n");
    
    for (int i = 0; i < NUM_ALLOCATIONS / 2; i++) {
        small_ptrs[i] = malloc(64);
        large_ptrs[i] = malloc(4096);
    }
    
    printf("  Allocated: %d small (64 bytes), %d large (4096 bytes)\n",
           NUM_ALLOCATIONS / 2, NUM_ALLOCATIONS / 2);
    printf("  Fragmentation: %.2f%%\n", stats_get_fragmentation_ratio() * 100.0);
    
    printf("Free all large\n");
    for (int i = 0; i < NUM_ALLOCATIONS / 2; i++) {
        free(large_ptrs[i]);
    }
    
    printf("  Fragmentation: %.2f%%\n", stats_get_fragmentation_ratio() * 100.0);
    printf("  Utilization: %.2f%%\n", stats_get_memory_utilization() * 100.0);
    
    printf("Free all small\n");
    for (int i = 0; i < NUM_ALLOCATIONS / 2; i++) {
        free(small_ptrs[i]);
    }
    
    printf("  Fragmentation: %.2f%%\n", stats_get_fragmentation_ratio() * 100.0);
    
    free(small_ptrs);
    free(large_ptrs);
}

void benchmark_gc_effect(void) {
    printf("\n=== GC Effect on Fragmentation ===\n");
    
    printf("Creating fragmentation pattern\n");
    
    for (int cycle = 0; cycle < 5; cycle++) {
        void** ptrs = (void**)malloc(10000 * sizeof(void*));
        
        for (int i = 0; i < 10000; i++) {
            ptrs[i] = malloc(random_size());
        }
        
        for (int i = 0; i < 5000; i++) {
            int idx = rand() % 10000;
            if (ptrs[idx]) {
                free(ptrs[idx]);
                ptrs[idx] = NULL;
            }
        }
        
        free(ptrs);
    }
    
    printf("  Before GC:\n");
    printf("    Fragmentation: %.2f%%\n", stats_get_fragmentation_ratio() * 100.0);
    printf("    Utilization: %.2f%%\n", stats_get_memory_utilization() * 100.0);
    
    gc_collect();
    
    printf("  After GC:\n");
    printf("    Fragmentation: %.2f%%\n", stats_get_fragmentation_ratio() * 100.0);
    printf("    Utilization: %.2f%%\n", stats_get_memory_utilization() * 100.0);
    
    struct nomalloc_gc_stats gc_stats;
    gc_get_allocator_stats(&gc_stats);
    printf("  GC stats: count=%llu, bytes_freed=%llu\n",
           gc_stats.gc_count, gc_stats.bytes_freed);
}

void benchmark_long_running(void) {
    printf("\n=== Long Running Fragmentation ===\n");
    
    struct allocation_info* allocs = (struct allocation_info*)malloc(
        10000 * sizeof(struct allocation_info));
    
    printf("Simulating long-running workload (%d cycles)\n", FRAGMENTATION_CYCLES);
    
    for (int cycle = 0; cycle < FRAGMENTATION_CYCLES; cycle++) {
        printf("Cycle %d:\n", cycle + 1);
        
        for (int i = 0; i < 10000; i++) {
            if (rand() % 3 == 0 && allocs[i].allocated) {
                free(allocs[i].ptr);
                allocs[i].allocated = 0;
            }
            
            if (!allocs[i].allocated && rand() % 2 == 0) {
                allocs[i].size = random_size();
                allocs[i].ptr = malloc(allocs[i].size);
                allocs[i].allocated = allocs[i].ptr != NULL;
            }
        }
        
        printf("  Fragmentation: %.2f%%, Utilization: %.2f%%\n",
               stats_get_fragmentation_ratio() * 100.0,
               stats_get_memory_utilization() * 100.0);
        
        if (cycle % 3 == 2) {
            gc_collect();
            printf("  After GC: Fragmentation: %.2f%%\n",
                   stats_get_fragmentation_ratio() * 100.0);
        }
    }
    
    for (int i = 0; i < 10000; i++) {
        if (allocs[i].allocated) {
            free(allocs[i].ptr);
        }
    }
    
    free(allocs);
    
    printf("Final state:\n");
    printf("  Fragmentation: %.2f%%\n", stats_get_fragmentation_ratio() * 100.0);
    printf("  Utilization: %.2f%%\n", stats_get_memory_utilization() * 100.0);
}

int main(void) {
    printf("=== Nomalloc Fragmentation Benchmark ===\n\n");
    
    srand(42);
    
    nomalloc_init();
    stats_enable();
    gc_enable();
    
    gc_set_watermark(0.7, 0.5);
    
    benchmark_fragmentation_pattern1();
    stats_reset();
    
    benchmark_fragmentation_pattern2();
    stats_reset();
    
    benchmark_fragmentation_pattern3();
    stats_reset();
    
    benchmark_gc_effect();
    stats_reset();
    
    benchmark_long_running();
    
    printf("\n=== Final Statistics ===\n");
    stats_print_summary();
    gc_print_stats();
    
    nomalloc_shutdown();
    
    return 0;
}