#include "size_class.h"
#include "../utils/log.h"
#include <stdio.h>

void size_class_print_info(void) {
    log_info("Size Class Information:");
    log_info("  Small classes (8B - 4KB): %zu classes", NUM_SMALL_CLASSES);
    
    printf("Small size classes:\n");
    for (size_t i = 0; i < NUM_SMALL_CLASSES; i++) {
        printf("  Class %zu: %zu bytes\n", i, small_size_classes[i]);
    }
    
    log_info("  Medium classes (4KB - 1MB): %zu classes", NUM_MEDIUM_CLASSES);
    
    printf("Medium size classes:\n");
    for (size_t i = 0; i < NUM_MEDIUM_CLASSES; i++) {
        printf("  Class %zu: %zu bytes\n", NUM_SMALL_CLASSES + i, medium_size_classes[i]);
    }
    
    log_info("  Large objects (>1MB): Direct allocation");
}