#include <nomalloc/nomalloc.h>
#include "../core/allocator.h"
#include "../utils/log.h"
#include <stdio.h>
#include <string.h>

int nomalloc_init(void) {
    return allocator_init();
}

void nomalloc_shutdown(void) {
    allocator_shutdown();
}

int nomalloc_startup(void) {
    return nomalloc_init();
}

void nomalloc_cleanup(void) {
    nomalloc_shutdown();
}

const char* nomalloc_version(void) {
    return NOMALLOC_VERSION;
}

const char* nomalloc_build_info(void) {
    static char build_info[256];
    
    snprintf(build_info, sizeof(build_info),
             "Nomalloc %s - Build: %s %s",
             NOMALLOC_VERSION,
             __DATE__, __TIME__);
    
    return build_info;
}

size_t nomalloc_get_total_allocated(void) {
    return allocator_get_total_allocated();
}

size_t nomalloc_get_total_freed(void) {
    return allocator_get_total_freed();
}

size_t nomalloc_get_active_allocations(void) {
    return allocator_get_active_allocations();
}

double nomalloc_get_fragmentation_ratio(void) {
    size_t allocated = allocator_get_total_allocated();
    size_t active = allocator_get_active_allocations();
    
    if (allocated == 0) return 0.0;
    
    return (double)(allocated - active) / (double)allocated;
}

double nomalloc_get_memory_utilization(void) {
    size_t allocated = allocator_get_total_allocated();
    size_t active = allocator_get_active_allocations();
    
    if (allocated == 0) return 100.0;
    
    return (double)active / (double)allocated * 100.0;
}

int nomalloc_print_stats_summary(void) {
    printf("\n=== Nomalloc Statistics Summary ===\n");
    printf("Version: %s\n", nomalloc_version());
    printf("Build: %s\n", nomalloc_build_info());
    printf("\n");
    printf("Memory Statistics:\n");
    printf("  Total allocated: %zu bytes (%.2f MB)\n",
           nomalloc_get_total_allocated(),
           nomalloc_get_total_allocated() / (1024.0 * 1024.0));
    printf("  Total freed:     %zu bytes (%.2f MB)\n",
           nomalloc_get_total_freed(),
           nomalloc_get_total_freed() / (1024.0 * 1024.0));
    printf("  Active:          %zu bytes (%.2f MB)\n",
           nomalloc_get_active_allocations(),
           nomalloc_get_active_allocations() / (1024.0 * 1024.0));
    printf("\n");
    printf("Efficiency:\n");
    printf("  Fragmentation:   %.2f%%\n", 
           nomalloc_get_fragmentation_ratio() * 100.0);
    printf("  Utilization:     %.2f%%\n",
           nomalloc_get_memory_utilization());
    
    allocator_print_stats();
    
    return 0;
}

int nomalloc_export_stats_json(const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        log_error("Failed to open file for JSON export: %s", filename);
        return -1;
    }
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": \"%s\",\n", nomalloc_version());
    fprintf(fp, "  \"build_info\": \"%s\",\n", nomalloc_build_info());
    fprintf(fp, "  \"statistics\": {\n");
    fprintf(fp, "    \"total_allocated\": %zu,\n", nomalloc_get_total_allocated());
    fprintf(fp, "    \"total_freed\": %zu,\n", nomalloc_get_total_freed());
    fprintf(fp, "    \"active_allocations\": %zu,\n", nomalloc_get_active_allocations());
    fprintf(fp, "    \"fragmentation_ratio\": %.4f,\n", nomalloc_get_fragmentation_ratio());
    fprintf(fp, "    \"memory_utilization\": %.4f\n", nomalloc_get_memory_utilization());
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    
    log_info("Statistics exported to JSON: %s", filename);
    return 0;
}

int nomalloc_export_stats_csv(const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        log_error("Failed to open file for CSV export: %s", filename);
        return -1;
    }
    
    fprintf(fp, "metric,value\n");
    fprintf(fp, "version,%s\n", nomalloc_version());
    fprintf(fp, "total_allocated,%zu\n", nomalloc_get_total_allocated());
    fprintf(fp, "total_freed,%zu\n", nomalloc_get_total_freed());
    fprintf(fp, "active_allocations,%zu\n", nomalloc_get_active_allocations());
    fprintf(fp, "fragmentation_ratio,%.4f\n", nomalloc_get_fragmentation_ratio());
    fprintf(fp, "memory_utilization,%.4f\n", nomalloc_get_memory_utilization());
    
    fclose(fp);
    
    log_info("Statistics exported to CSV: %s", filename);
    return 0;
}