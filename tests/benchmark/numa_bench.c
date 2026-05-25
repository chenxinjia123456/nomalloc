#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <nomalloc/nomalloc.h>

#ifdef NOMALLOC_NUMA_ENABLED
#include <numa.h>
#include <numaif.h>
#define NUMA_AVAILABLE 1
#else
#define NUMA_AVAILABLE 0
#endif

#define NUM_ALLOCATIONS 1000000
#define MAX_SIZE 8192
#define NUM_THREADS 8

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static size_t random_size(void) {
    return (size_t)(rand() % MAX_SIZE + 1);
}

void print_numa_topology(void) {
    printf("\n=== NUMA Topology ===\n");
    
    if (!NUMA_AVAILABLE) {
        printf("NUMA not available on this system\n");
        return;
    }
    
#ifdef NOMALLOC_NUMA_ENABLED
    if (numa_available() < 0) {
        printf("NUMA library not available\n");
        return;
    }
    
    int num_nodes = numa_max_node() + 1;
    int num_cpus = numa_num_configured_cpus();
    
    printf("NUMA nodes: %d\n", num_nodes);
    printf("CPUs: %d\n", num_cpus);
    
    for (int node = 0; node < num_nodes; node++) {
        long free_pages = 0;
        long total_pages = numa_node_size(node, &free_pages);
        
        printf("\nNode %d:\n", node);
        printf("  Total memory: %ld pages (%.2f MB)\n", 
               total_pages, total_pages * 4096 / (1024.0 * 1024.0));
        printf("  Free memory: %ld pages (%.2f MB)\n",
               free_pages, free_pages * 4096 / (1024.0 * 1024.0));
        
        struct bitmask* cpus = numa_allocate_cpumask();
        numa_node_to_cpus(node, cpus);
        
        printf("  CPUs: ");
        for (int i = 0; i < num_cpus; i++) {
            if (numa_bitmask_isbitset(cpus, i)) {
                printf("%d ", i);
            }
        }
        printf("\n");
        
        numa_free_cpumask(cpus);
        
        printf("  Distance to other nodes: ");
        for (int j = 0; j < num_nodes; j++) {
            printf("%d ", numa_distance(node, j));
        }
        printf("\n");
    }
#endif
}

void benchmark_local_allocation(void) {
    printf("\n=== Local NUMA Allocation Benchmark ===\n");
    
    if (!NUMA_AVAILABLE) {
        printf("NUMA not available, using standard allocation\n");
        
        double start = get_time_ms();
        for (int i = 0; i < NUM_ALLOCATIONS; i++) {
            void* ptr = malloc(random_size());
            if (ptr) free(ptr);
        }
        double end = get_time_ms();
        
        printf("Time: %.2f ms, Throughput: %.2f K ops/sec\n",
               end - start, NUM_ALLOCATIONS / (end - start) / 1000.0);
        return;
    }
    
#ifdef NOMALLOC_NUMA_ENABLED
    int current_node = numa_get_current_node();
    printf("Current node: %d\n", current_node);
    
    double start = get_time_ms();
    
    size_t allocated = 0;
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        size_t size = random_size();
        void* ptr = numa_alloc_onnode(size, current_node);
        if (ptr) {
            allocated += size;
            numa_free(ptr, size);
        }
    }
    
    double end = get_time_ms();
    
    printf("Local allocation time: %.2f ms\n", end - start);
    printf("Throughput: %.2f K ops/sec\n", NUM_ALLOCATIONS / (end - start) / 1000.0);
    printf("Allocated: %.2f MB\n", allocated / (1024.0 * 1024.0));
#endif
}

void benchmark_remote_allocation(void) {
    printf("\n=== Remote NUMA Allocation Benchmark ===\n");
    
    if (!NUMA_AVAILABLE) {
        printf("NUMA not available\n");
        return;
    }
    
#ifdef NOMALLOC_NUMA_ENABLED
    int num_nodes = numa_max_node() + 1;
    if (num_nodes < 2) {
        printf("Only one NUMA node, cannot test remote allocation\n");
        return;
    }
    
    int local_node = numa_get_current_node();
    int remote_node = (local_node + 1) % num_nodes;
    
    printf("Local node: %d, Remote node: %d\n", local_node, remote_node);
    printf("Distance: %d\n", numa_distance(local_node, remote_node));
    
    double local_start = get_time_ms();
    size_t local_allocated = 0;
    for (int i = 0; i < NUM_ALLOCATIONS / 2; i++) {
        size_t size = random_size();
        void* ptr = numa_alloc_onnode(size, local_node);
        if (ptr) {
            local_allocated += size;
            numa_free(ptr, size);
        }
    }
    double local_end = get_time_ms();
    
    double remote_start = get_time_ms();
    size_t remote_allocated = 0;
    for (int i = 0; i < NUM_ALLOCATIONS / 2; i++) {
        size_t size = random_size();
        void* ptr = numa_alloc_onnode(size, remote_node);
        if (ptr) {
            remote_allocated += size;
            numa_free(ptr, size);
        }
    }
    double remote_end = get_time_ms();
    
    printf("Local allocation: %.2f ms, %.2f K ops/sec\n",
           local_end - local_start, (NUM_ALLOCATIONS / 2) / (local_end - local_start) / 1000.0);
    printf("Remote allocation: %.2f ms, %.2f K ops/sec\n",
           remote_end - remote_start, (NUM_ALLOCATIONS / 2) / (remote_end - remote_start) / 1000.0);
    
    double overhead = (remote_end - remote_start - (local_end - local_start)) / 
                       (local_end - local_start) * 100.0;
    printf("Remote overhead: %.2f%%\n", overhead);
#endif
}

void benchmark_interleaved_allocation(void) {
    printf("\n=== Interleaved NUMA Allocation Benchmark ===\n");
    
    if (!NUMA_AVAILABLE) {
        printf("NUMA not available\n");
        return;
    }
    
#ifdef NOMALLOC_NUMA_ENABLED
    int num_nodes = numa_max_node() + 1;
    if (num_nodes < 2) {
        printf("Only one NUMA node\n");
        return;
    }
    
    printf("Interleaved allocation across %d nodes\n", num_nodes);
    
    double start = get_time_ms();
    
    size_t allocated = 0;
    for (int i = 0; i < NUM_ALLOCATIONS; i++) {
        size_t size = random_size();
        void* ptr = numa_alloc_interleaved(size);
        if (ptr) {
            allocated += size;
            numa_free(ptr, size);
        }
    }
    
    double end = get_time_ms();
    
    printf("Interleaved time: %.2f ms\n", end - start);
    printf("Throughput: %.2f K ops/sec\n", NUM_ALLOCATIONS / (end - start) / 1000.0);
#endif
}

struct numa_thread_data {
    int thread_id;
    int node;
    int iterations;
    double elapsed_ms;
    size_t allocated;
};

static void* numa_thread_func(void* arg) {
    struct numa_thread_data* data = (struct numa_thread_data*)arg;
    
    double start = get_time_ms();
    
    for (int i = 0; i < data->iterations; i++) {
        size_t size = random_size();
#ifdef NOMALLOC_NUMA_ENABLED
        void* ptr = numa_alloc_onnode(size, data->node);
#else
        void* ptr = malloc(size);
#endif
        if (ptr) {
            data->allocated += size;
#ifdef NOMALLOC_NUMA_ENABLED
            numa_free(ptr, size);
#else
            free(ptr);
#endif
        }
    }
    
    double end = get_time_ms();
    data->elapsed_ms = end - start;
    
    return NULL;
}

void benchmark_numa_thread_binding(void) {
    printf("\n=== NUMA Thread Binding Benchmark ===\n");
    
    if (!NUMA_AVAILABLE) {
        printf("NUMA not available\n");
        return;
    }
    
#ifdef NOMALLOC_NUMA_ENABLED
    int num_nodes = numa_max_node() + 1;
    
    printf("Testing thread binding to different nodes\n");
    
    pthread_t threads[NUM_THREADS];
    struct numa_thread_data data[NUM_THREADS];
    
    double start = get_time_ms();
    
    for (int i = 0; i < NUM_THREADS; i++) {
        data[i].thread_id = i;
        data[i].node = i % num_nodes;
        data[i].iterations = NUM_ALLOCATIONS / NUM_THREADS;
        data[i].allocated = 0;
        
        pthread_create(&threads[i], NULL, numa_thread_func, &data[i]);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end = get_time_ms();
    
    printf("Total time: %.2f ms\n", end - start);
    printf("Per-thread results:\n");
    
    size_t total_allocated = 0;
    double total_elapsed = 0.0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        printf("  Thread %d (node %d): %.2f ms, %.2f K ops/sec, %.2f MB\n",
               data[i].thread_id, data[i].node, data[i].elapsed_ms,
               data[i].iterations / data[i].elapsed_ms / 1000.0,
               data[i].allocated / (1024.0 * 1024.0));
        total_allocated += data[i].allocated;
        total_elapsed += data[i].elapsed_ms;
    }
    
    printf("Combined throughput: %.2f K ops/sec\n",
           NUM_ALLOCATIONS / (end - start) / 1000.0);
#endif
}

void benchmark_numa_memory_access(void) {
    printf("\n=== NUMA Memory Access Benchmark ===\n");
    
    if (!NUMA_AVAILABLE) {
        printf("NUMA not available\n");
        return;
    }
    
#ifdef NOMALLOC_NUMA_ENABLED
    int num_nodes = numa_max_node() + 1;
    if (num_nodes < 2) {
        printf("Only one NUMA node\n");
        return;
    }
    
    int local_node = numa_get_current_node();
    int remote_node = (local_node + 1) % num_nodes;
    
    size_t alloc_size = 100 * 1024 * 1024;
    
    void* local_mem = numa_alloc_onnode(alloc_size, local_node);
    void* remote_mem = numa_alloc_onnode(alloc_size, remote_node);
    
    if (!local_mem || !remote_mem) {
        printf("Allocation failed\n");
        if (local_mem) numa_free(local_mem, alloc_size);
        if (remote_mem) numa_free(remote_mem, alloc_size);
        return;
    }
    
    printf("Allocated %.2f MB on each node\n", alloc_size / (1024.0 * 1024.0));
    
    int iterations = 1000;
    
    double local_start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        memset(local_mem, i, alloc_size);
    }
    double local_end = get_time_ms();
    
    double remote_start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        memset(remote_mem, i, alloc_size);
    }
    double remote_end = get_time_ms();
    
    printf("Local memory write: %.2f ms (%.2f GB/s)\n",
           local_end - local_start,
           (double)iterations * alloc_size / (local_end - local_start) / 1000.0 / 1024.0);
    printf("Remote memory write: %.2f ms (%.2f GB/s)\n",
           remote_end - remote_start,
           (double)iterations * alloc_size / (remote_end - remote_start) / 1000.0 / 1024.0);
    
    double bandwidth_diff = ((double)iterations * alloc_size / (local_end - local_start) -
                              (double)iterations * alloc_size / (remote_end - remote_start)) /
                             ((double)iterations * alloc_size / (local_end - local_start)) * 100.0;
    printf("Bandwidth difference: %.2f%%\n", bandwidth_diff);
    
    numa_free(local_mem, alloc_size);
    numa_free(remote_mem, alloc_size);
#endif
}

int main(void) {
    printf("=== Nomalloc NUMA Benchmark ===\n\n");
    
    srand(42);
    
    nomalloc_init();
    
    printf("NUMA support: %s\n", NUMA_AVAILABLE ? "enabled" : "disabled");
    
    print_numa_topology();
    
    benchmark_local_allocation();
    benchmark_remote_allocation();
    benchmark_interleaved_allocation();
    benchmark_numa_thread_binding();
    benchmark_numa_memory_access();
    
    printf("\n=== Benchmark Complete ===\n");
    
    nomalloc_shutdown();
    
    return 0;
}