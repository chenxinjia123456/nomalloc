#include "numa.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/math.h"
#include "../utils/assert.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>

#ifdef NOMALLOC_NUMA_ENABLED
#include <numa.h>
#include <numaif.h>
#endif

struct numa_topology g_numa_topology;

static void numa_node_info_init(struct numa_node_info* info, int node_id) {
    memset(info, 0, sizeof(struct numa_node_info));
    info->node_id = node_id;
    
    atomic64_init(&info->allocations, 0);
    atomic64_init(&info->allocations_bytes, 0);
}

int numa_init(void) {
    memset(&g_numa_topology, 0, sizeof(g_numa_topology));
    
    spinlock_init(&g_numa_topology.lock);
    
    g_numa_topology.preferred_node = 0;
    g_numa_topology.current_node = 0;
    g_numa_topology.interleaved_allocations = false;
    
#ifdef NOMALLOC_NUMA_ENABLED
    if (numa_available() >= 0) {
        g_numa_topology.available = true;
        g_numa_topology.num_nodes = numa_max_node() + 1;
        g_numa_topology.num_cpus = numa_num_configured_cpus();
        
        for (int i = 0; i < g_numa_topology.num_nodes; i++) {
            numa_node_info_init(&g_numa_topology.nodes[i], i);
            
            g_numa_topology.nodes[i].total_memory = numa_node_size(i, NULL);
            g_numa_topology.nodes[i].free_memory = numa_node_size(i, &(long){0});
            g_numa_topology.nodes[i].used_memory = 
                g_numa_topology.nodes[i].total_memory - 
                g_numa_topology.nodes[i].free_memory;
            
            struct bitmask* cpus = numa_allocate_cpumask();
            numa_node_to_cpus(i, cpus);
            
            g_numa_topology.nodes[i].cpu_count = 0;
            for (int j = 0; j < numa_num_configured_cpus(); j++) {
                if (numa_bitmask_isbitset(cpus, j)) {
                    g_numa_topology.nodes[i].cpus[g_numa_topology.nodes[i].cpu_count++] = j;
                    g_numa_topology.cpu_to_node[j] = i;
                }
            }
            numa_free_cpumask(cpus);
        }
        
        for (int i = 0; i < g_numa_topology.num_nodes; i++) {
            for (int j = 0; j < g_numa_topology.num_nodes; j++) {
                g_numa_topology.node_distances[i][j] = numa_distance(i, j);
            }
        }
        
        g_numa_topology.initialized = true;
        
        log_info("NUMA initialized: nodes=%d, cpus=%d, total_memory=%zu bytes",
                 g_numa_topology.num_nodes, g_numa_topology.num_cpus,
                 numa_get_total_memory());
        
        return 0;
    }
#endif
    
    g_numa_topology.available = false;
    g_numa_topology.num_nodes = 1;
    g_numa_topology.num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    
    numa_node_info_init(&g_numa_topology.nodes[0], 0);
    g_numa_topology.nodes[0].total_memory = sysconf(_SC_PHYS_PAGES) * get_page_size();
    g_numa_topology.nodes[0].free_memory = sysconf(_SC_AVPHYS_PAGES) * get_page_size();
    g_numa_topology.nodes[0].used_memory = 
        g_numa_topology.nodes[0].total_memory - g_numa_topology.nodes[0].free_memory;
    g_numa_topology.nodes[0].cpu_count = g_numa_topology.num_cpus;
    
    for (int i = 0; i < g_numa_topology.num_cpus; i++) {
        g_numa_topology.nodes[0].cpus[i] = i;
        g_numa_topology.cpu_to_node[i] = 0;
    }
    
    g_numa_topology.node_distances[0][0] = 10;
    
    g_numa_topology.initialized = true;
    
    log_info("NUMA not available, using single node configuration: cpus=%d, memory=%zu bytes",
             g_numa_topology.num_cpus, numa_get_total_memory());
    
    return 0;
}

void numa_shutdown(void) {
    if (!g_numa_topology.initialized) {
        return;
    }
    
    spinlock_lock(&g_numa_topology.lock);
    
    memset(&g_numa_topology.nodes, 0, sizeof(g_numa_topology.nodes));
    memset(&g_numa_topology.cpu_to_node, 0, sizeof(g_numa_topology.cpu_to_node));
    memset(&g_numa_topology.node_distances, 0, sizeof(g_numa_topology.node_distances));
    
    g_numa_topology.initialized = false;
    g_numa_topology.available = false;
    
    spinlock_unlock(&g_numa_topology.lock);
    
    log_info("NUMA shutdown");
}

bool numa_is_available(void) {
    return g_numa_topology.available;
}

int numa_get_num_nodes(void) {
    return g_numa_topology.num_nodes;
}

int numa_get_num_cpus(void) {
    return g_numa_topology.num_cpus;
}

int numa_get_node_for_cpu(int cpu) {
    if (cpu < 0 || cpu >= NUMA_MAX_CPUS) {
        return NUMA_DEFAULT_NODE;
    }
    return g_numa_topology.cpu_to_node[cpu];
}

int numa_get_current_cpu(void) {
#ifdef NOMALLOC_NUMA_ENABLED
    if (g_numa_topology.available) {
        return sched_getcpu();
    }
#endif
    return 0;
}

int numa_get_current_node(void) {
    if (!g_numa_topology.available) {
        return 0;
    }
    
    int cpu = numa_get_current_cpu();
    return numa_get_node_for_cpu(cpu);
}

int numa_get_preferred_node(void) {
    return g_numa_topology.preferred_node;
}

int numa_set_preferred_node(int node) {
    if (node < 0 || node >= g_numa_topology.num_nodes) {
        return -1;
    }
    
    g_numa_topology.preferred_node = node;
    
#ifdef NOMALLOC_NUMA_ENABLED
    if (g_numa_topology.available) {
        numa_set_preferred(node);
    }
#endif
    
    log_debug("NUMA preferred node set to %d", node);
    return 0;
}

size_t numa_get_node_memory(int node) {
    if (node < 0 || node >= g_numa_topology.num_nodes) {
        return 0;
    }
    return g_numa_topology.nodes[node].total_memory;
}

size_t numa_get_node_free_memory(int node) {
    if (node < 0 || node >= g_numa_topology.num_nodes) {
        return 0;
    }
    return g_numa_topology.nodes[node].free_memory;
}

size_t numa_get_total_memory(void) {
    size_t total = 0;
    for (int i = 0; i < g_numa_topology.num_nodes; i++) {
        total += g_numa_topology.nodes[i].total_memory;
    }
    return total;
}

size_t numa_get_total_free_memory(void) {
    size_t free = 0;
    for (int i = 0; i < g_numa_topology.num_nodes; i++) {
        free += g_numa_topology.nodes[i].free_memory;
    }
    return free;
}

int numa_get_node_distance(int node1, int node2) {
    if (node1 < 0 || node1 >= g_numa_topology.num_nodes ||
        node2 < 0 || node2 >= g_numa_topology.num_nodes) {
        return 255;
    }
    return g_numa_topology.node_distances[node1][node2];
}

int numa_get_nearest_node(int node) {
    if (node < 0 || node >= g_numa_topology.num_nodes) {
        return NUMA_DEFAULT_NODE;
    }
    
    int nearest = -1;
    int min_distance = 255;
    
    for (int i = 0; i < g_numa_topology.num_nodes; i++) {
        if (i != node) {
            int dist = numa_get_node_distance(node, i);
            if (dist < min_distance && numa_get_node_free_memory(i) > 0) {
                min_distance = dist;
                nearest = i;
            }
        }
    }
    
    return nearest;
}

void* numa_alloc_on_node(size_t size, int node) {
    void* ptr = NULL;
    
#ifdef NOMALLOC_NUMA_ENABLED
    if (g_numa_topology.available && node >= 0 && node < g_numa_topology.num_nodes) {
        ptr = numa_alloc_onnode(size, node);
        
        if (ptr) {
            atomic64_inc(&g_numa_topology.nodes[node].allocations);
            atomic64_add_fetch(&g_numa_topology.nodes[node].allocations_bytes, size);
        }
    }
#endif
    
    if (!ptr) {
        ptr = memory_alloc_aligned(size, get_page_size());
    }
    
    log_trace("NUMA alloc on node %d: ptr=%p, size=%zu", node, ptr, size);
    return ptr;
}

void* numa_alloc_local(size_t size) {
    return numa_alloc_on_node(size, numa_get_current_node());
}

void* numa_alloc_interleaved(size_t size) {
    void* ptr = NULL;
    
#ifdef NOMALLOC_NUMA_ENABLED
    if (g_numa_topology.available && g_numa_topology.interleaved_allocations) {
        ptr = numa_alloc_interleaved(size);
        
        if (ptr) {
            for (int i = 0; i < g_numa_topology.num_nodes; i++) {
                atomic64_inc(&g_numa_topology.nodes[i].allocations);
            }
        }
    }
#endif
    
    if (!ptr) {
        ptr = numa_alloc_on_node(size, g_numa_topology.preferred_node);
    }
    
    log_trace("NUMA alloc interleaved: ptr=%p, size=%zu", ptr, size);
    return ptr;
}

void numa_free(void* ptr, size_t size) {
    if (!ptr) return;
    
#ifdef NOMALLOC_NUMA_ENABLED
    if (g_numa_topology.available) {
        numa_free(ptr, size);
    } else {
        memory_free_aligned(ptr, size, get_page_size());
    }
#else
    memory_free_aligned(ptr, size, get_page_size());
#endif
    
    log_trace("NUMA free: ptr=%p, size=%zu", ptr, size);
}

int numa_bind_to_node(int node) {
#ifdef NOMALLOC_NUMA_ENABLED
    if (g_numa_topology.available && node >= 0 && node < g_numa_topology.num_nodes) {
        struct bitmask* mask = numa_allocate_nodemask();
        numa_bitmask_setbit(mask, node);
        numa_bind(mask);
        numa_free_nodemask(mask);
        return 0;
    }
#endif
    return -1;
}

int numa_bind_to_nodes(int* nodes, int num_nodes) {
#ifdef NOMALLOC_NUMA_ENABLED
    if (g_numa_topology.available && nodes && num_nodes > 0) {
        struct bitmask* mask = numa_allocate_nodemask();
        for (int i = 0; i < num_nodes; i++) {
            if (nodes[i] >= 0 && nodes[i] < g_numa_topology.num_nodes) {
                numa_bitmask_setbit(mask, nodes[i]);
            }
        }
        numa_bind(mask);
        numa_free_nodemask(mask);
        return 0;
    }
#endif
    return -1;
}

int numa_bind_to_cpu(int cpu) {
#ifdef NOMALLOC_NUMA_ENABLED
    if (g_numa_topology.available && cpu >= 0 && cpu < g_numa_topology.num_cpus) {
        struct bitmask* mask = numa_allocate_cpumask();
        numa_bitmask_setbit(mask, cpu);
        numa_sched_setaffinity(0, mask);
        numa_free_cpumask(mask);
        return 0;
    }
#endif
    return -1;
}

int numa_bind_to_cpus(int* cpus, int num_cpus) {
#ifdef NOMALLOC_NUMA_ENABLED
    if (g_numa_topology.available && cpus && num_cpus > 0) {
        struct bitmask* mask = numa_allocate_cpumask();
        for (int i = 0; i < num_cpus; i++) {
            if (cpus[i] >= 0 && cpus[i] < g_numa_topology.num_cpus) {
                numa_bitmask_setbit(mask, cpus[i]);
            }
        }
        numa_sched_setaffinity(0, mask);
        numa_free_cpumask(mask);
        return 0;
    }
#endif
    return -1;
}

void numa_print_topology(void) {
    printf("\nNUMA Topology:\n");
    printf("  Available: %s\n", g_numa_topology.available ? "yes" : "no");
    printf("  Initialized: %s\n", g_numa_topology.initialized ? "yes" : "no");
    printf("  Num nodes: %d\n", g_numa_topology.num_nodes);
    printf("  Num CPUs: %d\n", g_numa_topology.num_cpus);
    printf("  Preferred node: %d\n", g_numa_topology.preferred_node);
    printf("  Interleaved allocations: %s\n", 
           g_numa_topology.interleaved_allocations ? "yes" : "no");
    printf("  Total memory: %zu bytes (%.2f GB)\n", 
           numa_get_total_memory(), numa_get_total_memory() / (1024.0 * 1024.0 * 1024.0));
    printf("  Total free memory: %zu bytes (%.2f GB)\n", 
           numa_get_total_free_memory(), numa_get_total_free_memory() / (1024.0 * 1024.0 * 1024.0));
    
    printf("\n  Node distances:\n");
    for (int i = 0; i < g_numa_topology.num_nodes; i++) {
        printf("    Node %d: ", i);
        for (int j = 0; j < g_numa_topology.num_nodes; j++) {
            printf("%3d ", g_numa_topology.node_distances[i][j]);
        }
        printf("\n");
    }
    
    for (int i = 0; i < g_numa_topology.num_nodes; i++) {
        numa_print_node_info(i);
    }
}

void numa_print_node_info(int node) {
    if (node < 0 || node >= g_numa_topology.num_nodes) {
        return;
    }
    
    struct numa_node_info* info = &g_numa_topology.nodes[node];
    
    printf("\n  Node %d:\n", node);
    printf("    Total memory: %zu bytes (%.2f GB)\n", 
           info->total_memory, info->total_memory / (1024.0 * 1024.0 * 1024.0));
    printf("    Free memory: %zu bytes (%.2f GB)\n", 
           info->free_memory, info->free_memory / (1024.0 * 1024.0 * 1024.0));
    printf("    Used memory: %zu bytes (%.2f GB)\n", 
           info->used_memory, info->used_memory / (1024.0 * 1024.0 * 1024.0));
    printf("    CPU count: %d\n", info->cpu_count);
    printf("    CPUs: ");
    for (int i = 0; i < info->cpu_count; i++) {
        printf("%d ", info->cpus[i]);
    }
    printf("\n");
    printf("    Allocations: %llu\n", atomic64_load(&info->allocations));
    printf("    Allocated bytes: %llu\n", atomic64_load(&info->allocations_bytes));
}