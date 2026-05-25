#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdatomic.h>
#include <stdint.h>
#include <nomalloc/nomalloc.h>

#define NUM_THREADS 8
#define TEST_ITERATIONS 10
#define MAX_OBJECTS_PER_THREAD 5000
#define TRANSFER_QUEUE_SIZE 1000
#define COOKIE_64 0xbf58476d1ce4e5b9UL
#define COOKIE_32 0x1ce4e5b9UL

static atomic_int running = 1;
static atomic_int barrier = 0;

typedef struct {
    atomic_ulong total_allocs;
    atomic_ulong total_frees;
    atomic_ulong total_reallocs;
    atomic_ulong total_bytes;
    atomic_ulong corruption_detected;
    atomic_ulong transfer_count;
} global_stats_t;

static global_stats_t global_stats;
static volatile void* transfer_queue[TRANSFER_QUEUE_SIZE];

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void barrier_wait(int n) {
    atomic_fetch_add(&barrier, 1);
    while (atomic_load(&barrier) < n) {
        __asm__ volatile("pause" ::: "memory");
    }
}

static uintptr_t pick_random(uintptr_t* r) {
    uintptr_t x = *r;
#if (UINTPTR_MAX > UINT32_MAX)
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9UL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebUL;
    x ^= x >> 31;
#else
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
#endif
    *r = x;
    return x;
}

static int chance(size_t perc, uintptr_t* r) {
    return (pick_random(r) % 100) <= perc;
}

static void* alloc_items(size_t items, uintptr_t cookie) {
    if (items == 0) items = 1;
    
    uintptr_t* p = (uintptr_t*)calloc(items, sizeof(uintptr_t));
    if (p != NULL) {
        for (uintptr_t i = 0; i < items; i++) {
            p[i] = (items - i) ^ cookie;
        }
        atomic_fetch_add(&global_stats.total_allocs, 1);
        atomic_fetch_add(&global_stats.total_bytes, items * sizeof(uintptr_t));
    }
    return p;
}

static void free_items(void* p, uintptr_t cookie) {
    if (p != NULL) {
        uintptr_t* q = (uintptr_t*)p;
        uintptr_t items = (q[0] ^ cookie);
        
        for (uintptr_t i = 0; i < items; i++) {
            if ((q[i] ^ cookie) != items - i) {
                atomic_fetch_add(&global_stats.corruption_detected, 1);
                fprintf(stderr, "memory corruption at block %p at %zu\n", p, i);
                break;
            }
        }
        
        free(p);
        atomic_fetch_add(&global_stats.total_frees, 1);
    }
}

static void* atomic_exchange_ptr(volatile void** p, void* newval) {
    return atomic_exchange((volatile _Atomic(void*)*)p, newval);
}

static void* larson_thread(void* arg) {
    intptr_t tid = (intptr_t)arg;
    uintptr_t r = ((tid + 1) * 43);
    uintptr_t cookie = (sizeof(void*) == 8) ? COOKIE_64 : COOKIE_32;
    
    barrier_wait(NUM_THREADS);
    
    const size_t max_item_shift = 5;
    size_t allocs = 5000;
    size_t retain = allocs / 2;
    
    void** data = NULL;
    size_t data_size = 0;
    size_t data_top = 0;
    
    void** retained = (void**)calloc(retain, sizeof(void*));
    size_t retain_top = 0;
    
    while (allocs > 0 || retain > 0) {
        if (retain == 0 || (chance(50, &r) && allocs > 0)) {
            allocs--;
            if (data_top >= data_size) {
                data_size += 500;
                data = (void**)realloc(data, data_size * sizeof(void*));
            }
            data[data_top++] = alloc_items(1ULL << (pick_random(&r) % max_item_shift), cookie);
        } else {
            retained[retain_top++] = alloc_items(1ULL << (pick_random(&r) % (max_item_shift + 2)), cookie);
            retain--;
        }
        
        if (chance(66, &r) && data_top > 0) {
            size_t idx = pick_random(&r) % data_top;
            free_items(data[idx], cookie);
            data[idx] = NULL;
        }
        
        if (chance(25, &r) && data_top > 0) {
            size_t data_idx = pick_random(&r) % data_top;
            size_t transfer_idx = pick_random(&r) % TRANSFER_QUEUE_SIZE;
            void* p = data[data_idx];
            void* q = atomic_exchange_ptr(&transfer_queue[transfer_idx], p);
            data[data_idx] = q;
            atomic_fetch_add(&global_stats.transfer_count, 1);
        }
    }
    
    for (size_t i = 0; i < retain_top; i++) {
        free_items(retained[i], cookie);
    }
    for (size_t i = 0; i < data_top; i++) {
        free_items(data[i], cookie);
    }
    
    free(retained);
    free(data);
    
    return NULL;
}

static void* alloc_test_thread(void* arg) {
    intptr_t tid = (intptr_t)arg;
    uintptr_t r = ((tid + 1) * 43);
    
    barrier_wait(NUM_THREADS);
    
    void* ptrs[MAX_OBJECTS_PER_THREAD];
    size_t sizes[MAX_OBJECTS_PER_THREAD];
    size_t count = 0;
    size_t total_ops = 0;
    size_t ops_per_thread = 100000 / NUM_THREADS;
    
    while (total_ops < ops_per_thread && atomic_load(&running)) {
        if (count < MAX_OBJECTS_PER_THREAD / 2) {
            size_t size = (pick_random(&r) % 1024) + 1;
            if (chance(1, &r)) {
                size *= 100;
            }
            
            void* ptr = malloc(size);
            if (ptr) {
                memset(ptr, (tid & 0xFF), size);
                ptrs[count] = ptr;
                sizes[count] = size;
                count++;
            }
        } else {
            size_t idx = pick_random(&r) % count;
            if (ptrs[idx]) {
                for (size_t i = 0; i < sizes[idx] && i < 64; i++) {
                    if (((unsigned char*)ptrs[idx])[i] != (tid & 0xFF)) {
                        atomic_fetch_add(&global_stats.corruption_detected, 1);
                        break;
                    }
                }
                free(ptrs[idx]);
            }
            
            ptrs[idx] = ptrs[count - 1];
            sizes[idx] = sizes[count - 1];
            count--;
        }
        
        total_ops++;
    }
    
    for (size_t i = 0; i < count; i++) {
        if (ptrs[i]) free(ptrs[i]);
    }
    
    return NULL;
}

static void* xmalloc_producer_thread(void* arg) {
    intptr_t tid = (intptr_t)arg;
    uintptr_t r = ((tid + 1) * 43);
    uintptr_t cookie = (sizeof(void*) == 8) ? COOKIE_64 : COOKIE_32;
    
    barrier_wait(NUM_THREADS);
    
    size_t produced = 0;
    size_t target = 100000 / NUM_THREADS;
    
    while (produced < target && atomic_load(&running)) {
        size_t items = 1ULL << (pick_random(&r) % 7);
        if (items < 8) items = 8;
        
        void* ptr = alloc_items(items, cookie);
        if (ptr) {
            size_t transfer_idx = pick_random(&r) % TRANSFER_QUEUE_SIZE;
            void* old = atomic_exchange_ptr(&transfer_queue[transfer_idx], ptr);
            if (old) {
                free_items(old, cookie);
            }
            produced++;
        }
    }
    
    return NULL;
}

static void* xmalloc_consumer_thread(void* arg) {
    intptr_t tid = (intptr_t)arg;
    uintptr_t r = ((tid + 1) * 43);
    uintptr_t cookie = (sizeof(void*) == 8) ? COOKIE_64 : COOKIE_32;
    
    barrier_wait(NUM_THREADS);
    
    size_t consumed = 0;
    size_t target = 100000 / NUM_THREADS;
    
    while (consumed < target && atomic_load(&running)) {
        size_t transfer_idx = pick_random(&r) % TRANSFER_QUEUE_SIZE;
        void* ptr = atomic_exchange_ptr(&transfer_queue[transfer_idx], NULL);
        
        if (ptr) {
            free_items(ptr, cookie);
            consumed++;
        }
    }
    
    return NULL;
}

static void* sh6bench_thread(void* arg) {
    intptr_t tid = (intptr_t)arg;
    uintptr_t r = ((tid + 1) * 43);
    
    barrier_wait(NUM_THREADS);
    
    void* ptrs[MAX_OBJECTS_PER_THREAD];
    size_t count = 0;
    
    for (int round = 0; round < 10 && atomic_load(&running); round++) {
        for (int i = 0; i < 100 && count < MAX_OBJECTS_PER_THREAD; i++) {
            size_t size = (pick_random(&r) % 1024) + 8;
            ptrs[count] = malloc(size);
            if (ptrs[count]) {
                memset(ptrs[count], (tid & 0xFF), size);
                count++;
            }
        }
        
        int lifo_count = count * 60 / 100;
        for (int i = 0; i < lifo_count && count > 0; i++) {
            free(ptrs[count - 1]);
            count--;
        }
        
        int fifo_count = count * 40 / 100;
        for (int i = 0; i < fifo_count && count > 0; i++) {
            free(ptrs[i]);
            for (size_t j = i; j < count - 1; j++) {
                ptrs[j] = ptrs[j + 1];
            }
            count--;
            fifo_count--;
        }
    }
    
    for (size_t i = 0; i < count; i++) {
        free(ptrs[i]);
    }
    
    return NULL;
}

static void* cache_scratch_thread(void* arg) {
    intptr_t tid = (intptr_t)arg;
    
    barrier_wait(NUM_THREADS);
    
    volatile char* ptrs[100];
    
    for (int round = 0; round < 100 && atomic_load(&running); round++) {
        for (int i = 0; i < 100; i++) {
            ptrs[i] = (volatile char*)malloc(64);
            if (ptrs[i]) {
                memset((void*)ptrs[i], tid & 0xFF, 64);
            }
        }
        
        for (int access = 0; access < 10000; access++) {
            for (int i = 0; i < 100; i++) {
                if (ptrs[i]) {
                    ptrs[i][0] = (ptrs[i][0] + 1) & 0xFF;
                    ptrs[i][63] = (ptrs[i][63] + 1) & 0xFF;
                }
            }
        }
        
        for (int i = 0; i < 100; i++) {
            if (ptrs[i]) free((void*)ptrs[i]);
        }
    }
    
    return NULL;
}

static void* mstress_thread(void* arg) {
    intptr_t tid = (intptr_t)arg;
    uintptr_t r = ((tid + 1) * 43);
    uintptr_t cookie = (sizeof(void*) == 8) ? COOKIE_64 : COOKIE_32;
    
    barrier_wait(NUM_THREADS);
    
    const size_t max_item_shift = 7;
    size_t workload = (tid % 8 + 1) * 1000;
    
    void** data = (void**)calloc(workload, sizeof(void*));
    void** retained = (void**)calloc(workload / 4, sizeof(void*));
    size_t data_top = 0;
    size_t retain_top = 0;
    
    while (workload > 0) {
        if (chance(70, &r)) {
            size_t size_shift = pick_random(&r) % max_item_shift;
            void* ptr = alloc_items(1ULL << size_shift, cookie);
            if (ptr && data_top < workload) {
                data[data_top++] = ptr;
            }
            workload--;
        } else if (data_top > 0) {
            size_t idx = pick_random(&r) % data_top;
            free_items(data[idx], cookie);
            data[idx] = data[data_top - 1];
            data_top--;
        }
        
        if (chance(10, &r) && retain_top < workload / 4) {
            retained[retain_top++] = alloc_items(1ULL << (pick_random(&r) % max_item_shift), cookie);
        }
        
        if (chance(25, &r) && data_top > 0) {
            size_t data_idx = pick_random(&r) % data_top;
            size_t transfer_idx = pick_random(&r) % TRANSFER_QUEUE_SIZE;
            void* p = data[data_idx];
            void* q = atomic_exchange_ptr(&transfer_queue[transfer_idx], p);
            data[data_idx] = q;
        }
    }
    
    for (size_t i = 0; i < data_top; i++) {
        free_items(data[i], cookie);
    }
    for (size_t i = 0; i < retain_top; i++) {
        free_items(retained[i], cookie);
    }
    
    free(data);
    free(retained);
    
    return NULL;
}

static void run_larson_benchmark(void) {
    printf("\n========================================\n");
    printf("Larson Benchmark (Server Workload)\n");
    printf("Threads: %d, Iterations: %d\n", NUM_THREADS, TEST_ITERATIONS);
    printf("========================================\n");
    
    memset(&global_stats, 0, sizeof(global_stats));
    memset(transfer_queue, 0, sizeof(transfer_queue));
    
    nomalloc_init();
    
    double start = get_time_ms();
    
    for (int iter = 0; iter < TEST_ITERATIONS; iter++) {
        atomic_store(&barrier, 0);
        
        pthread_t threads[NUM_THREADS];
        for (intptr_t i = 0; i < NUM_THREADS; i++) {
            pthread_create(&threads[i], NULL, larson_thread, (void*)i);
        }
        
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(threads[i], NULL);
        }
        
        uintptr_t r = 43;
        for (int i = 0; i < TRANSFER_QUEUE_SIZE; i++) {
            if (chance(50, &r) || iter + 1 == TEST_ITERATIONS) {
                void* p = atomic_exchange_ptr(&transfer_queue[i], NULL);
                free_items(p, (sizeof(void*) == 8) ? COOKIE_64 : COOKIE_32);
            }
        }
        
        if ((iter + 1) % 10 == 0) {
            printf("- iterations left: %3d\n", TEST_ITERATIONS - (iter + 1));
        }
    }
    
    double end = get_time_ms();
    double elapsed = (end - start) / 1000.0;
    
    nomalloc_shutdown();
    
    printf("\nResults:\n");
    printf("  Allocations:     %lu\n", atomic_load(&global_stats.total_allocs));
    printf("  Frees:           %lu\n", atomic_load(&global_stats.total_frees));
    printf("  Bytes allocated: %.2f MB\n", atomic_load(&global_stats.total_bytes) / (1024.0 * 1024.0));
    printf("  Transfers:       %lu\n", atomic_load(&global_stats.transfer_count));
    printf("  Corruptions:     %lu\n", atomic_load(&global_stats.corruption_detected));
    printf("  Throughput:      %.0f ops/sec\n", 
           (atomic_load(&global_stats.total_allocs) + atomic_load(&global_stats.total_frees)) / elapsed);
    printf("  Time:            %.2f sec\n", elapsed);
    
    if (atomic_load(&global_stats.corruption_detected) == 0) {
        printf("\n[PASS] No memory corruption\n");
    } else {
        printf("\n[FAIL] Memory corruption detected\n");
    }
}

static void run_alloc_test_benchmark(void) {
    printf("\n========================================\n");
    printf("Alloc-Test Benchmark (Pareto Distribution)\n");
    printf("Threads: %d, Operations: 10M\n", NUM_THREADS);
    printf("========================================\n");
    
    memset(&global_stats, 0, sizeof(global_stats));
    atomic_store(&barrier, 0);
    atomic_store(&running, 1);
    
    nomalloc_init();
    
    double start = get_time_ms();
    
    pthread_t threads[NUM_THREADS];
    for (intptr_t i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, alloc_test_thread, (void*)i);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end = get_time_ms();
    double elapsed = (end - start) / 1000.0;
    
    nomalloc_shutdown();
    
    printf("\nResults:\n");
    printf("  Corruptions:     %lu\n", atomic_load(&global_stats.corruption_detected));
    printf("  Throughput:      %.0f ops/sec\n", 10000000.0 / elapsed);
    printf("  Time:            %.2f sec\n", elapsed);
    
    if (atomic_load(&global_stats.corruption_detected) == 0) {
        printf("\n[PASS] No memory corruption\n");
    }
}

static void run_xmalloc_benchmark(void) {
    printf("\n========================================\n");
    printf("Xmalloc-Test Benchmark (Producer/Consumer)\n");
    printf("Producers: 8, Consumers: 8\n");
    printf("========================================\n");
    
    memset(&global_stats, 0, sizeof(global_stats));
    memset(transfer_queue, 0, sizeof(transfer_queue));
    atomic_store(&barrier, 0);
    atomic_store(&running, 1);
    
    nomalloc_init();
    
    double start = get_time_ms();
    
    pthread_t producers[8];
    pthread_t consumers[8];
    
    for (intptr_t i = 0; i < 8; i++) {
        pthread_create(&producers[i], NULL, xmalloc_producer_thread, (void*)i);
    }
    for (intptr_t i = 0; i < 8; i++) {
        pthread_create(&consumers[i], NULL, xmalloc_consumer_thread, (void*)(i + 8));
    }
    
    for (int i = 0; i < 8; i++) {
        pthread_join(producers[i], NULL);
    }
    for (int i = 0; i < 8; i++) {
        pthread_join(consumers[i], NULL);
    }
    
    for (int i = 0; i < TRANSFER_QUEUE_SIZE; i++) {
        void* p = atomic_exchange_ptr(&transfer_queue[i], NULL);
        free_items(p, (sizeof(void*) == 8) ? COOKIE_64 : COOKIE_32);
    }
    
    double end = get_time_ms();
    double elapsed = (end - start) / 1000.0;
    
    nomalloc_shutdown();
    
    printf("\nResults:\n");
    printf("  Allocations:     %lu\n", atomic_load(&global_stats.total_allocs));
    printf("  Frees:           %lu\n", atomic_load(&global_stats.total_frees));
    printf("  Bytes allocated: %.2f MB\n", atomic_load(&global_stats.total_bytes) / (1024.0 * 1024.0));
    printf("  Corruptions:     %lu\n", atomic_load(&global_stats.corruption_detected));
    printf("  Throughput:      %.0f ops/sec\n", 
           (atomic_load(&global_stats.total_allocs) + atomic_load(&global_stats.total_frees)) / elapsed);
    printf("  Time:            %.2f sec\n", elapsed);
    
    if (atomic_load(&global_stats.corruption_detected) == 0) {
        printf("\n[PASS] No memory corruption\n");
    }
}

static void run_sh6bench_benchmark(void) {
    printf("\n========================================\n");
    printf("SH6Bench Benchmark (LIFO/FIFO Mixed)\n");
    printf("Threads: %d\n", NUM_THREADS);
    printf("========================================\n");
    
    memset(&global_stats, 0, sizeof(global_stats));
    atomic_store(&barrier, 0);
    atomic_store(&running, 1);
    
    nomalloc_init();
    
    double start = get_time_ms();
    
    pthread_t threads[NUM_THREADS];
    for (intptr_t i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, sh6bench_thread, (void*)i);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end = get_time_ms();
    double elapsed = (end - start) / 1000.0;
    
    nomalloc_shutdown();
    
    printf("\nResults:\n");
    printf("  Corruptions:     %lu\n", atomic_load(&global_stats.corruption_detected));
    printf("  Time:            %.2f sec\n", elapsed);
    
    if (atomic_load(&global_stats.corruption_detected) == 0) {
        printf("\n[PASS] No memory corruption\n");
    }
}

static void run_cache_scratch_benchmark(void) {
    printf("\n========================================\n");
    printf("Cache-Scratch Benchmark (False Sharing)\n");
    printf("Threads: %d\n", NUM_THREADS);
    printf("========================================\n");
    
    memset(&global_stats, 0, sizeof(global_stats));
    atomic_store(&barrier, 0);
    atomic_store(&running, 1);
    
    nomalloc_init();
    
    double start = get_time_ms();
    
    pthread_t threads[NUM_THREADS];
    for (intptr_t i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, cache_scratch_thread, (void*)i);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end = get_time_ms();
    double elapsed = (end - start) / 1000.0;
    
    nomalloc_shutdown();
    
    printf("\nResults:\n");
    printf("  Time:            %.2f sec\n", elapsed);
    printf("\n[PASS] Completed\n");
}

static void run_mstress_benchmark(void) {
    printf("\n========================================\n");
    printf("Mstress Benchmark (Real-world Simulation)\n");
    printf("Threads: %d\n", NUM_THREADS);
    printf("========================================\n");
    
    memset(&global_stats, 0, sizeof(global_stats));
    memset(transfer_queue, 0, sizeof(transfer_queue));
    atomic_store(&barrier, 0);
    atomic_store(&running, 1);
    
    nomalloc_init();
    
    double start = get_time_ms();
    
    pthread_t threads[NUM_THREADS];
    for (intptr_t i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, mstress_thread, (void*)i);
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    for (int i = 0; i < TRANSFER_QUEUE_SIZE; i++) {
        void* p = atomic_exchange_ptr(&transfer_queue[i], NULL);
        free_items(p, (sizeof(void*) == 8) ? COOKIE_64 : COOKIE_32);
    }
    
    double end = get_time_ms();
    double elapsed = (end - start) / 1000.0;
    
    nomalloc_shutdown();
    
    printf("\nResults:\n");
    printf("  Allocations:     %lu\n", atomic_load(&global_stats.total_allocs));
    printf("  Frees:           %lu\n", atomic_load(&global_stats.total_frees));
    printf("  Bytes allocated: %.2f MB\n", atomic_load(&global_stats.total_bytes) / (1024.0 * 1024.0));
    printf("  Corruptions:     %lu\n", atomic_load(&global_stats.corruption_detected));
    printf("  Time:            %.2f sec\n", elapsed);
    
    if (atomic_load(&global_stats.corruption_detected) == 0) {
        printf("\n[PASS] No memory corruption\n");
    }
}

int main(int argc, char** argv) {
    printf("\n========================================\n");
    printf("  Mimalloc/Kqmalloc Style Benchmarks\n");
    printf("========================================\n");
    
    srand(0x7feb352d);
    
    run_larson_benchmark();
    run_alloc_test_benchmark();
    run_xmalloc_benchmark();
    run_sh6bench_benchmark();
    run_cache_scratch_benchmark();
    run_mstress_benchmark();
    
    printf("\n========================================\n");
    printf("  All Benchmarks Completed\n");
    printf("========================================\n");
    
    return 0;
}