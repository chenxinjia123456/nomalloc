#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <nomalloc/nomalloc.h>

#define NUM_THREADS 8
#define TEST_DURATION_SEC 10
#define MAX_OBJECTS 5000
#define SMALL_SIZE_MAX 256
#define MEDIUM_SIZE_MAX 4096
#define LARGE_SIZE_MAX 65536
#define HUGE_SIZE (1024 * 1024)

static volatile int running = 1;
static volatile int phase = 0;

typedef struct {
    int thread_id;
    size_t total_allocs;
    size_t total_frees;
    size_t total_bytes_alloc;
    size_t total_bytes_free;
    size_t alloc_failures;
    size_t current_active;
    size_t max_active;
    double min_latency_us;
    double max_latency_us;
    double total_latency_us;
    size_t latency_count;
} thread_stats_t;

static thread_stats_t thread_stats[NUM_THREADS];
static double start_time;

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

static void update_latency(thread_stats_t* stats, double latency) {
    stats->latency_count++;
    stats->total_latency_us += latency;
    
    if (latency < stats->min_latency_us || stats->min_latency_us == 0) {
        stats->min_latency_us = latency;
    }
    if (latency > stats->max_latency_us) {
        stats->max_latency_us = latency;
    }
}

static void* small_object_stress(void* arg) {
    int thread_id = *(int*)arg;
    thread_stats_t* stats = &thread_stats[thread_id];
    void* ptrs[MAX_OBJECTS];
    size_t sizes[MAX_OBJECTS];
    size_t count = 0;
    
    while (running) {
        if (rand() % 100 < 70 && count < MAX_OBJECTS) {
            size_t size = (rand() % SMALL_SIZE_MAX) + 1;
            
            double start = get_time_us();
            void* ptr = malloc(size);
            double end = get_time_us();
            
            if (ptr) {
                update_latency(stats, end - start);
                memset(ptr, thread_id & 0xFF, size);
                ptrs[count] = ptr;
                sizes[count] = size;
                count++;
                
                stats->total_allocs++;
                stats->total_bytes_alloc += size;
                stats->current_active++;
                if (stats->current_active > stats->max_active) {
                    stats->max_active = stats->current_active;
                }
            } else {
                stats->alloc_failures++;
            }
        } else if (count > 0) {
            size_t idx = rand() % count;
            
            double start = get_time_us();
            free(ptrs[idx]);
            double end = get_time_us();
            
            update_latency(stats, end - start);
            
            stats->total_bytes_free += sizes[idx];
            stats->total_frees++;
            stats->current_active--;
            
            ptrs[idx] = ptrs[count - 1];
            sizes[idx] = sizes[count - 1];
            count--;
        }
    }
    
    for (size_t i = 0; i < count; i++) {
        free(ptrs[i]);
        stats->total_frees++;
        stats->current_active--;
    }
    
    return NULL;
}

static void* large_object_stress(void* arg) {
    int thread_id = *(int*)arg;
    thread_stats_t* stats = &thread_stats[thread_id];
    void* ptrs[100];
    size_t sizes[100];
    size_t count = 0;
    
    while (running) {
        if (rand() % 100 < 50 && count < 100) {
            size_t size;
            int type = rand() % 3;
            if (type == 0) {
                size = (rand() % MEDIUM_SIZE_MAX) + 1;
            } else if (type == 1) {
                size = ((rand() % LARGE_SIZE_MAX) + 1);
            } else {
                size = HUGE_SIZE;
            }
            
            double start = get_time_us();
            void* ptr = malloc(size);
            double end = get_time_us();
            
            if (ptr) {
                update_latency(stats, end - start);
                memset(ptr, (thread_id + 1) & 0xFF, size > 1024 ? 1024 : size);
                ptrs[count] = ptr;
                sizes[count] = size;
                count++;
                
                stats->total_allocs++;
                stats->total_bytes_alloc += size;
                stats->current_active++;
                if (stats->current_active > stats->max_active) {
                    stats->max_active = stats->current_active;
                }
            } else {
                stats->alloc_failures++;
            }
        } else if (count > 0) {
            size_t idx = rand() % count;
            
            double start = get_time_us();
            free(ptrs[idx]);
            double end = get_time_us();
            
            update_latency(stats, end - start);
            
            stats->total_bytes_free += sizes[idx];
            stats->total_frees++;
            stats->current_active--;
            
            ptrs[idx] = ptrs[count - 1];
            sizes[idx] = sizes[count - 1];
            count--;
        }
    }
    
    for (size_t i = 0; i < count; i++) {
        free(ptrs[i]);
        stats->total_frees++;
    }
    
    return NULL;
}

static void* mixed_stress(void* arg) {
    int thread_id = *(int*)arg;
    thread_stats_t* stats = &thread_stats[thread_id];
    void* ptrs[MAX_OBJECTS];
    size_t sizes[MAX_OBJECTS];
    size_t count = 0;
    
    while (running) {
        int op = rand() % 100;
        
        if (op < 40 && count < MAX_OBJECTS) {
            size_t size = (rand() % SMALL_SIZE_MAX) + 1;
            void* ptr = malloc(size);
            if (ptr) {
                memset(ptr, thread_id & 0xFF, size);
                ptrs[count] = ptr;
                sizes[count] = size;
                count++;
                stats->total_allocs++;
                stats->total_bytes_alloc += size;
                stats->current_active++;
                if (stats->current_active > stats->max_active) {
                    stats->max_active = stats->current_active;
                }
            }
        } else if (op < 50 && count < MAX_OBJECTS) {
            size_t size = ((rand() % LARGE_SIZE_MAX) + 1);
            void* ptr = malloc(size);
            if (ptr) {
                memset(ptr, (thread_id + 1) & 0xFF, size > 1024 ? 1024 : size);
                ptrs[count] = ptr;
                sizes[count] = size;
                count++;
                stats->total_allocs++;
                stats->total_bytes_alloc += size;
                stats->current_active++;
                if (stats->current_active > stats->max_active) {
                    stats->max_active = stats->current_active;
                }
            }
        } else if (op < 60 && count < MAX_OBJECTS) {
            size_t size = (rand() % MEDIUM_SIZE_MAX) + 1;
            void* ptr = malloc(size);
            if (ptr) {
                memset(ptr, thread_id & 0xFF, size > 1024 ? 1024 : size);
                ptrs[count] = ptr;
                sizes[count] = size;
                count++;
                stats->total_allocs++;
                stats->total_bytes_alloc += size;
                stats->current_active++;
                if (stats->current_active > stats->max_active) {
                    stats->max_active = stats->current_active;
                }
            }
        } else if (op < 70 && count < MAX_OBJECTS) {
            size_t nmemb = rand() % 10 + 1;
            size_t elem_size = rand() % 100 + 1;
            size_t total_size = nmemb * elem_size;
            void* ptr = calloc(nmemb, elem_size);
            if (ptr) {
                ptrs[count] = ptr;
                sizes[count] = total_size;
                count++;
                stats->total_allocs++;
                stats->total_bytes_alloc += total_size;
                stats->current_active++;
                if (stats->current_active > stats->max_active) {
                    stats->max_active = stats->current_active;
                }
            }
        } else if (op < 85 && count > 0) {
            size_t idx = rand() % count;
            free(ptrs[idx]);
            stats->total_bytes_free += sizes[idx];
            stats->total_frees++;
            stats->current_active--;
            ptrs[idx] = ptrs[count - 1];
            sizes[idx] = sizes[count - 1];
            count--;
        } else if (op < 95 && count > 0) {
            size_t idx = rand() % count;
            size_t new_size = (rand() % 10000) + 1;
            void* ptr = realloc(ptrs[idx], new_size);
            if (ptr) {
                ptrs[idx] = ptr;
                stats->total_bytes_alloc += new_size > sizes[idx] ? new_size - sizes[idx] : 0;
                sizes[idx] = new_size;
            } else {
                stats->alloc_failures++;
            }
        } else if (count > 0) {
            size_t idx = rand() % count;
            free(ptrs[idx]);
            stats->total_bytes_free += sizes[idx];
            stats->total_frees++;
            stats->current_active--;
            ptrs[idx] = ptrs[count - 1];
            sizes[idx] = sizes[count - 1];
            count--;
        }
    }
    
    for (size_t i = 0; i < count; i++) {
        free(ptrs[i]);
        stats->total_frees++;
    }
    
    return NULL;
}

static void run_phase(const char* name, void* (*func)(void*), int duration_sec) {
    printf("\n=== Phase: %s ===\n", name);
    printf("Duration: %d seconds, Threads: %d\n", duration_sec, NUM_THREADS);
    
    running = 1;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        memset(&thread_stats[i], 0, sizeof(thread_stats_t));
        thread_stats[i].thread_id = i;
        thread_stats[i].min_latency_us = 0;
    }
    
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    double start = get_time_ms();
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, func, &thread_ids[i]);
    }
    
    sleep(duration_sec);
    
    running = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end = get_time_ms();
    double elapsed = (end - start) / 1000.0;
    
    size_t total_allocs = 0, total_frees = 0;
    size_t total_bytes_alloc = 0, total_bytes_free = 0;
    size_t total_failures = 0;
    double total_latency = 0;
    size_t total_latency_count = 0;
    double min_latency = 1e9, max_latency = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        total_allocs += thread_stats[i].total_allocs;
        total_frees += thread_stats[i].total_frees;
        total_bytes_alloc += thread_stats[i].total_bytes_alloc;
        total_bytes_free += thread_stats[i].total_bytes_free;
        total_failures += thread_stats[i].alloc_failures;
        total_latency += thread_stats[i].total_latency_us;
        total_latency_count += thread_stats[i].latency_count;
        
        if (thread_stats[i].min_latency_us > 0 && 
            thread_stats[i].min_latency_us < min_latency) {
            min_latency = thread_stats[i].min_latency_us;
        }
        if (thread_stats[i].max_latency_us > max_latency) {
            max_latency = thread_stats[i].max_latency_us;
        }
    }
    
    printf("\nResults:\n");
    printf("  Total allocations:  %zu\n", total_allocs);
    printf("  Total frees:        %zu\n", total_frees);
    printf("  Total bytes alloc:  %.2f MB\n", total_bytes_alloc / (1024.0 * 1024.0));
    printf("  Total bytes freed:  %.2f MB\n", total_bytes_free / (1024.0 * 1024.0));
    printf("  Allocation failures: %zu\n", total_failures);
    printf("  Throughput:         %.0f ops/sec\n", (total_allocs + total_frees) / elapsed);
    printf("  Bandwidth:          %.2f MB/sec\n", 
           (total_bytes_alloc + total_bytes_free) / (1024.0 * 1024.0) / elapsed);
    printf("  Avg latency:        %.2f us\n", 
           total_latency_count > 0 ? total_latency / total_latency_count : 0);
    printf("  Min latency:        %.2f us\n", min_latency < 1e9 ? min_latency : 0);
    printf("  Max latency:        %.2f us\n", max_latency);
}

static void print_allocator_stats(void) {
    printf("\n=== Allocator Statistics ===\n");
    nomalloc_print_stats_summary();
}

int main(void) {
    printf("\n========================================\n");
    printf("    Nomalloc Pressure Test Suite\n");
    printf("========================================\n");
    
    srand(time(NULL));
    nomalloc_init();
    
    run_phase("Small Object Stress", small_object_stress, 10);
    print_allocator_stats();
    
    run_phase("Large Object Stress", large_object_stress, 10);
    print_allocator_stats();
    
    run_phase("Mixed Operations Stress", mixed_stress, 10);
    print_allocator_stats();
    
    nomalloc_shutdown();
    
    printf("\n=== Pressure Test Completed ===\n");
    return 0;
}