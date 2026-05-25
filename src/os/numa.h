#ifndef NOMALLOC_OS_NUMA_H
#define NOMALLOC_OS_NUMA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include "../utils/atomic.h"
#include "../utils/spinlock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUMA_MAX_NODES 16
#define NUMA_MAX_CPUS  256
#define NUMA_DEFAULT_NODE -1

int numa_get_current_cpu(void);

struct numa_node_info {
    int node_id;
    size_t total_memory;
    size_t free_memory;
    size_t used_memory;
    
    int cpu_count;
    int cpus[NUMA_MAX_CPUS];
    
    double distance_ratio[NUMA_MAX_NODES];
    
    atomic64_t allocations;
    atomic64_t allocations_bytes;
};

struct numa_topology {
    bool available;
    bool initialized;
    
    int num_nodes;
    int num_cpus;
    
    struct numa_node_info nodes[NUMA_MAX_NODES];
    
    int cpu_to_node[NUMA_MAX_CPUS];
    int node_distances[NUMA_MAX_NODES][NUMA_MAX_NODES];
    
    int preferred_node;
    int current_node;
    
    bool interleaved_allocations;
    
    spinlock_t lock;
};

extern struct numa_topology g_numa_topology;

int numa_init(void);
void numa_shutdown(void);

bool numa_is_available(void);
int numa_get_num_nodes(void);
int numa_get_num_cpus(void);

int numa_get_node_for_cpu(int cpu);
int numa_get_current_node(void);
int numa_get_preferred_node(void);
int numa_set_preferred_node(int node);

size_t numa_get_node_memory(int node);
size_t numa_get_node_free_memory(int node);
size_t numa_get_total_memory(void);
size_t numa_get_total_free_memory(void);

int numa_get_node_distance(int node1, int node2);
int numa_get_nearest_node(int node);

void* numa_alloc_on_node(size_t size, int node);
void* numa_alloc_local(size_t size);
void* numa_alloc_interleaved(size_t size);
void numa_free(void* ptr, size_t size);

int numa_bind_to_node(int node);
int numa_bind_to_nodes(int* nodes, int num_nodes);
int numa_bind_to_cpu(int cpu);
int numa_bind_to_cpus(int* cpus, int num_cpus);

void numa_print_topology(void);
void numa_print_node_info(int node);

int numa_get_current_cpu(void);

static inline int numa_get_current_cpu_node(void) {
    int cpu = numa_get_current_cpu();
    if (cpu >= 0 && cpu < NUMA_MAX_CPUS) {
        return g_numa_topology.cpu_to_node[cpu];
    }
    return NUMA_DEFAULT_NODE;
}

static inline bool numa_should_interleave(void) {
    return g_numa_topology.interleaved_allocations;
}

static inline void numa_set_interleave(bool enable) {
    g_numa_topology.interleaved_allocations = enable;
}

static inline int numa_get_best_node_for_size(size_t size) {
    if (!numa_is_available()) {
        return NUMA_DEFAULT_NODE;
    }
    
    int best_node = g_numa_topology.preferred_node;
    size_t best_free = numa_get_node_free_memory(best_node);
    
    for (int i = 0; i < g_numa_topology.num_nodes; i++) {
        if (i != best_node) {
            size_t free = numa_get_node_free_memory(i);
            int dist = numa_get_node_distance(best_node, i);
            
            if (free > best_free && dist < 20) {
                best_node = i;
                best_free = free;
            }
        }
    }
    
    return best_node;
}

static inline double numa_get_distance_ratio(int node1, int node2) {
    if (!numa_is_available()) return 1.0;
    
    int distance = numa_get_node_distance(node1, node2);
    int local_distance = numa_get_node_distance(node1, node1);
    
    if (local_distance == 0) return 1.0;
    
    return (double)distance / (double)local_distance;
}

#ifdef __cplusplus
}
#endif

#endif