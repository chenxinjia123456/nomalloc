#include "leak_detector.h"
#include "../utils/memory.h"
#include "../utils/log.h"
#include "../utils/time.h"
#include "../utils/assert.h"
#include "../core/size_class.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <execinfo.h>

struct leak_detector g_leak_detector;

static inline uint32_t leak_hash_ptr(void* ptr) {
    return hash32_ptr(ptr);
}

static inline size_t leak_hash_index(void* ptr) {
    return leak_hash_ptr(ptr) & (LEAK_DETECTOR_HASH_SIZE - 1);
}

static int capture_stack_trace(void** buffer, int max_depth) {
    return backtrace(buffer, max_depth);
}

static void print_stack_trace(void** stack, int depth) {
    char** symbols = backtrace_symbols(stack, depth);
    if (symbols) {
        for (int i = 0; i < depth; i++) {
            printf("    [%d] %s\n", i, symbols[i]);
        }
        free(symbols);
    }
}

int leak_detector_init(void) {
    if (g_leak_detector.enabled) {
        log_warn("Leak detector already initialized");
        return 0;
    }
    
    memset(&g_leak_detector, 0, sizeof(g_leak_detector));
    
    g_leak_detector.max_records = LEAK_DETECTOR_MAX_ALLOCATIONS;
    g_leak_detector.records = (struct allocation_record**)malloc(
        g_leak_detector.max_records * sizeof(struct allocation_record*));
    
    if (!g_leak_detector.records) {
        log_error("Failed to allocate leak detector records");
        return -1;
    }
    
    for (size_t i = 0; i < LEAK_DETECTOR_HASH_SIZE; i++) {
        INIT_HLIST_HEAD(&g_leak_detector.hash_table[i]);
    }
    
    INIT_LIST_HEAD(&g_leak_detector.allocation_list);
    INIT_LIST_HEAD(&g_leak_detector.free_list);
    
    spinlock_init(&g_leak_detector.lock);
    
    atomic64_init(&g_leak_detector.stats.total_allocations, 0);
    atomic64_init(&g_leak_detector.stats.total_deallocations, 0);
    atomic64_init(&g_leak_detector.stats.leaked_allocations, 0);
    atomic64_init(&g_leak_detector.stats.leaked_bytes, 0);
    atomic64_init(&g_leak_detector.stats.peak_allocations, 0);
    atomic64_init(&g_leak_detector.stats.peak_bytes, 0);
    atomic8_init(&g_leak_detector.stats.tracking_active, 0);
    
    g_leak_detector.enabled = true;
    g_leak_detector.tracking = false;
    g_leak_detector.verbose = false;
    
    log_info("Leak detector initialized: max_records=%zu", g_leak_detector.max_records);
    
    return 0;
}

void leak_detector_shutdown(void) {
    if (!g_leak_detector.enabled) {
        return;
    }
    
    spinlock_lock(&g_leak_detector.lock);
    
    struct allocation_record* record;
    struct allocation_record* next;
    
    list_for_each_entry_safe(record, next, &g_leak_detector.allocation_list, list) {
        list_del(&record->list);
        hlist_del(&record->hash_node);
        free(record);
    }
    
    list_for_each_entry_safe(record, next, &g_leak_detector.free_list, list) {
        list_del(&record->list);
        free(record);
    }
    
    free(g_leak_detector.records);
    g_leak_detector.records = NULL;
    g_leak_detector.num_records = 0;
    
    g_leak_detector.enabled = false;
    g_leak_detector.tracking = false;
    
    spinlock_unlock(&g_leak_detector.lock);
    
    log_info("Leak detector shutdown: total_allocations=%llu, leaked=%llu",
             atomic64_load(&g_leak_detector.stats.total_allocations),
             leak_detector_get_leaked_count());
}

int leak_detector_enable(void) {
    if (!g_leak_detector.enabled) {
        return leak_detector_init();
    }
    
    g_leak_detector.tracking = true;
    atomic8_store(&g_leak_detector.stats.tracking_active, 1);
    
    log_debug("Leak detector enabled and tracking started");
    return 0;
}

int leak_detector_disable(void) {
    if (!g_leak_detector.enabled) {
        return -1;
    }
    
    g_leak_detector.tracking = false;
    atomic8_store(&g_leak_detector.stats.tracking_active, 0);
    
    log_debug("Leak detector disabled");
    return 0;
}

bool leak_detector_is_enabled(void) {
    return g_leak_detector.enabled && g_leak_detector.tracking;
}

int leak_detector_start_tracking(void) {
    return leak_detector_enable();
}

int leak_detector_stop_tracking(void) {
    return leak_detector_disable();
}

bool leak_detector_is_tracking(void) {
    return atomic8_load(&g_leak_detector.stats.tracking_active) != 0;
}

void leak_detector_record_alloc(void* ptr, size_t size, size_t class_idx, int arena_id) {
    if (!g_leak_detector.enabled || !g_leak_detector.tracking || !ptr) {
        return;
    }
    
    spinlock_lock(&g_leak_detector.lock);
    
    struct allocation_record* record = NULL;
    
    if (!list_empty(&g_leak_detector.free_list)) {
        record = list_first_entry(&g_leak_detector.free_list, 
                                  struct allocation_record, list);
        list_del(&record->list);
    } else if (g_leak_detector.num_records < g_leak_detector.max_records) {
        record = (struct allocation_record*)malloc(sizeof(struct allocation_record));
        if (!record) {
            spinlock_unlock(&g_leak_detector.lock);
            log_error("Failed to allocate leak detector record");
            return;
        }
    } else {
        spinlock_unlock(&g_leak_detector.lock);
        log_warn("Leak detector record limit reached");
        return;
    }
    
    record->ptr = ptr;
    record->size = size;
    record->class_idx = class_idx;
    record->arena_id = arena_id;
    record->thread_id = pthread_self();
    record->timestamp = get_time_ns();
    
    record->stack_depth = capture_stack_trace(record->stack_trace, 
                                               LEAK_DETECTOR_STACK_DEPTH);
    
    INIT_HLIST_NODE(&record->hash_node);
    INIT_LIST_HEAD(&record->list);
    
    size_t hash_idx = leak_hash_index(ptr);
    hlist_add_head(&record->hash_node, &g_leak_detector.hash_table[hash_idx]);
    list_add_tail(&record->list, &g_leak_detector.allocation_list);
    
    g_leak_detector.num_records++;
    
    atomic64_inc(&g_leak_detector.stats.total_allocations);
    
    uint64_t peak = atomic64_load(&g_leak_detector.stats.peak_allocations);
    uint64_t current = g_leak_detector.num_records;
    if (current > peak) {
        atomic64_store(&g_leak_detector.stats.peak_allocations, current);
        atomic64_add_fetch(&g_leak_detector.stats.peak_bytes, size);
    }
    
    spinlock_unlock(&g_leak_detector.lock);
    
    if (g_leak_detector.verbose) {
        log_trace("Leak detector: alloc recorded ptr=%p size=%zu", ptr, size);
    }
}

void leak_detector_record_dealloc(void* ptr) {
    if (!g_leak_detector.enabled || !g_leak_detector.tracking || !ptr) {
        return;
    }
    
    spinlock_lock(&g_leak_detector.lock);
    
    size_t hash_idx = leak_hash_index(ptr);
    
    struct allocation_record* record = NULL;
    struct allocation_record* r;
    
    hlist_for_each_entry(r, &g_leak_detector.hash_table[hash_idx], hash_node) {
        if (r->ptr == ptr) {
            record = r;
            break;
        }
    }
    
    if (!record) {
        spinlock_unlock(&g_leak_detector.lock);
        return;
    }
    
    hlist_del(&record->hash_node);
    list_del(&record->list);
    
    record->ptr = NULL;
    record->size = 0;
    
    list_add_tail(&record->list, &g_leak_detector.free_list);
    
    g_leak_detector.num_records--;
    
    atomic64_inc(&g_leak_detector.stats.total_deallocations);
    
    spinlock_unlock(&g_leak_detector.lock);
    
    if (g_leak_detector.verbose) {
        log_trace("Leak detector: dealloc recorded ptr=%p", ptr);
    }
}

void leak_detector_update_stats(void) {
    spinlock_lock(&g_leak_detector.lock);
    
    uint64_t leaked = g_leak_detector.num_records;
    uint64_t leaked_bytes = 0;
    
    struct allocation_record* record;
    list_for_each_entry(record, &g_leak_detector.allocation_list, list) {
        leaked_bytes += record->size;
    }
    
    atomic64_store(&g_leak_detector.stats.leaked_allocations, leaked);
    atomic64_store(&g_leak_detector.stats.leaked_bytes, leaked_bytes);
    
    spinlock_unlock(&g_leak_detector.lock);
}

int leak_detector_get_stats(struct nomalloc_leak_stats* stats) {
    if (!stats) return -1;
    
    leak_detector_update_stats();
    
    stats->leaked_objects = leak_detector_get_leaked_count();
    stats->leaked_bytes = leak_detector_get_leaked_bytes();
    stats->total_allocated = atomic64_load(&g_leak_detector.stats.total_allocations);
    stats->total_freed = atomic64_load(&g_leak_detector.stats.total_deallocations);
    stats->active_allocations = stats->total_allocated - stats->total_freed;
    
    return 0;
}

static int generate_leak_report(struct leak_report* report) {
    if (!report) return -1;
    
    memset(report, 0, sizeof(struct leak_report));
    
    spinlock_lock(&g_leak_detector.lock);
    
    struct allocation_record* record;
    list_for_each_entry(record, &g_leak_detector.allocation_list, list) {
        if (report->num_entries >= LEAK_DETECTOR_REPORT_SIZE) {
            break;
        }
        
        int found = -1;
        for (size_t i = 0; i < report->num_entries; i++) {
            if (report->entries[i].size == record->size &&
                report->entries[i].stack_depth == record->stack_depth) {
                
                bool match = true;
                for (int j = 0; j < record->stack_depth && j < LEAK_DETECTOR_STACK_DEPTH; j++) {
                    if (report->entries[i].common_stack_trace[j] != record->stack_trace[j]) {
                        match = false;
                        break;
                    }
                }
                
                if (match) {
                    found = i;
                    break;
                }
            }
        }
        
        if (found >= 0) {
            report->entries[found].count++;
            report->entries[found].total_bytes += record->size;
            report->entries[found].last_seen = record->timestamp;
        } else {
            struct leak_report_entry* entry = &report->entries[report->num_entries];
            entry->size = record->size;
            entry->count = 1;
            entry->total_bytes = record->size;
            entry->first_seen = record->timestamp;
            entry->last_seen = record->timestamp;
            entry->stack_depth = record->stack_depth;
            
            for (int i = 0; i < record->stack_depth && i < LEAK_DETECTOR_STACK_DEPTH; i++) {
                entry->common_stack_trace[i] = record->stack_trace[i];
            }
            
            report->num_entries++;
        }
        
        report->total_leaked_bytes += record->size;
        report->total_leaked_objects++;
    }
    
    spinlock_unlock(&g_leak_detector.lock);
    
    return 0;
}

int leak_detector_generate_report(const char* filename) {
    struct leak_report report;
    if (generate_leak_report(&report) != 0) {
        return -1;
    }
    
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        log_error("Failed to open leak report file: %s", filename);
        return -2;
    }
    
    fprintf(fp, "Leak Detection Report\n");
    fprintf(fp, "=====================\n\n");
    fprintf(fp, "Total leaked objects: %llu\n", report.total_leaked_objects);
    fprintf(fp, "Total leaked bytes: %llu\n", report.total_leaked_bytes);
    fprintf(fp, "Unique leak patterns: %zu\n\n", report.num_entries);
    
    for (size_t i = 0; i < report.num_entries; i++) {
        struct leak_report_entry* entry = &report.entries[i];
        
        fprintf(fp, "Leak Pattern %zu:\n", i + 1);
        fprintf(fp, "  Size: %zu bytes\n", entry->size);
        fprintf(fp, "  Count: %llu occurrences\n", entry->count);
        fprintf(fp, "  Total bytes: %llu\n", entry->total_bytes);
        fprintf(fp, "  Stack trace:\n");
        
        char** symbols = backtrace_symbols(entry->common_stack_trace, entry->stack_depth);
        if (symbols) {
            for (int j = 0; j < entry->stack_depth; j++) {
                fprintf(fp, "    [%d] %s\n", j, symbols[j]);
            }
            free(symbols);
        }
        
        fprintf(fp, "\n");
    }
    
    fclose(fp);
    
    log_info("Leak report generated: %s", filename);
    return 0;
}

int leak_detector_print_report(void) {
    struct leak_report report;
    if (generate_leak_report(&report) != 0) {
        return -1;
    }
    
    printf("\nLeak Detection Report\n");
    printf("=====================\n\n");
    printf("Total leaked objects: %llu\n", report.total_leaked_objects);
    printf("Total leaked bytes: %llu (%.2f KB)\n", 
           report.total_leaked_bytes, report.total_leaked_bytes / 1024.0);
    printf("Unique leak patterns: %zu\n\n", report.num_entries);
    
    for (size_t i = 0; i < report.num_entries; i++) {
        struct leak_report_entry* entry = &report.entries[i];
        
        printf("Leak Pattern %zu:\n", i + 1);
        printf("  Size: %zu bytes\n", entry->size);
        printf("  Count: %llu occurrences\n", entry->count);
        printf("  Total bytes: %llu\n", entry->total_bytes);
        printf("  Stack trace:\n");
        
        print_stack_trace(entry->common_stack_trace, entry->stack_depth);
        printf("\n");
    }
    
    return 0;
}

int leak_detector_clear(void) {
    spinlock_lock(&g_leak_detector.lock);
    
    struct allocation_record* record;
    struct allocation_record* next;
    
    list_for_each_entry_safe(record, next, &g_leak_detector.allocation_list, list) {
        list_del(&record->list);
        hlist_del(&record->hash_node);
        list_add_tail(&record->list, &g_leak_detector.free_list);
    }
    
    g_leak_detector.num_records = 0;
    
    atomic64_store(&g_leak_detector.stats.leaked_allocations, 0);
    atomic64_store(&g_leak_detector.stats.leaked_bytes, 0);
    
    spinlock_unlock(&g_leak_detector.lock);
    
    log_debug("Leak detector cleared");
    return 0;
}

void leak_detector_dump_summary(void) {
    leak_detector_update_stats();
    
    printf("\nLeak Detector Summary:\n");
    printf("  Enabled: %s\n", g_leak_detector.enabled ? "yes" : "no");
    printf("  Tracking: %s\n", g_leak_detector.tracking ? "yes" : "no");
    printf("  Verbose: %s\n", g_leak_detector.verbose ? "yes" : "no");
    printf("  Current allocations: %zu\n", g_leak_detector.num_records);
    printf("  Peak allocations: %llu\n", 
           atomic64_load(&g_leak_detector.stats.peak_allocations));
    printf("  Total allocations recorded: %llu\n", 
           atomic64_load(&g_leak_detector.stats.total_allocations));
    printf("  Total deallocations recorded: %llu\n", 
           atomic64_load(&g_leak_detector.stats.total_deallocations));
    printf("  Leaked objects: %llu\n", leak_detector_get_leaked_count());
    printf("  Leaked bytes: %llu (%.2f KB)\n", 
           leak_detector_get_leaked_bytes(), 
           leak_detector_get_leaked_bytes() / 1024.0);
    printf("  Leak ratio: %.2f%%\n", leak_detector_get_leak_ratio() * 100);
}