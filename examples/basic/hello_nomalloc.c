#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nomalloc.h>

int main(void) {
    printf("=== Nomalloc Basic Usage Example ===\n\n");
    
    if (nomalloc_init() != 0) {
        printf("Failed to initialize nomalloc\n");
        return 1;
    }
    
    printf("Allocator initialized successfully\n");
    printf("Version: %s\n\n", nomalloc_version());
    
    printf("1. Basic malloc/free:\n");
    void* ptr1 = malloc(100);
    if (ptr1) {
        printf("   Allocated 100 bytes at %p\n", ptr1);
        memset(ptr1, 0xAA, 100);
        printf("   Filled with pattern 0xAA\n");
        free(ptr1);
        printf("   Freed successfully\n\n");
    }
    
    printf("2. Calloc (zeroed memory):\n");
    void* ptr2 = calloc(10, 100);
    if (ptr2) {
        printf("   Allocated 10 x 100 = 1000 zeroed bytes at %p\n", ptr2);
        
        unsigned char* bytes = (unsigned char*)ptr2;
        bool all_zero = true;
        for (int i = 0; i < 1000; i++) {
            if (bytes[i] != 0) {
                all_zero = false;
                break;
            }
        }
        
        printf("   Memory is %s\n\n", all_zero ? "zeroed" : "NOT zeroed");
        free(ptr2);
    }
    
    printf("3. Realloc:\n");
    void* ptr3 = malloc(100);
    if (ptr3) {
        printf("   Allocated 100 bytes at %p\n", ptr3);
        memset(ptr3, 0xBB, 100);
        
        ptr3 = realloc(ptr3, 200);
        if (ptr3) {
            printf("   Reallocated to 200 bytes at %p\n", ptr3);
            
            unsigned char* bytes = (unsigned char*)ptr3;
            bool preserved = true;
            for (int i = 0; i < 100; i++) {
                if (bytes[i] != 0xBB) {
                    preserved = false;
                    break;
                }
            }
            
            printf("   Original data %s\n\n", preserved ? "preserved" : "NOT preserved");
            free(ptr3);
        }
    }
    
    printf("4. Aligned allocation:\n");
    void* ptr4 = aligned_alloc(256, 1024);
    if (ptr4) {
        printf("   Allocated 1024 bytes aligned to 256 at %p\n", ptr4);
        
        uintptr_t addr = (uintptr_t)ptr4;
        if (addr % 256 == 0) {
            printf("   Address is properly aligned (%zu %% 256 == 0)\n\n", addr);
        } else {
            printf("   Address NOT aligned (%zu %% 256 != 0)\n\n", addr);
        }
        free(ptr4);
    }
    
    printf("5. Usable size:\n");
    void* ptr5 = malloc(100);
    if (ptr5) {
        size_t usable = malloc_usable_size(ptr5);
        printf("   Allocated 100 bytes at %p\n", ptr5);
        printf("   Usable size: %zu bytes (>= requested)\n\n", usable);
        free(ptr5);
    }
    
    printf("6. Statistics:\n");
    printf("   Total allocated: %zu bytes\n", nomalloc_get_total_allocated());
    printf("   Total freed:     %zu bytes\n", nomalloc_get_total_freed());
    printf("   Active:          %zu bytes\n", nomalloc_get_active_allocations());
    printf("   Fragmentation:   %.2f%%\n", nomalloc_get_fragmentation_ratio() * 100);
    printf("   Utilization:     %.2f%%\n", nomalloc_get_memory_utilization() * 100);
    
    nomalloc_print_stats_summary();
    
    nomalloc_shutdown();
    
    printf("\nAllocator shutdown successfully\n");
    printf("\nExample completed successfully!\n");
    
    return 0;
}