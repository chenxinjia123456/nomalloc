#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <nomalloc/nomalloc.h>

int main(void) {
    printf("Step 1: Starting test\n");
    fflush(stdout);
    
    printf("Step 2: Calling nomalloc_init\n");
    fflush(stdout);
    int ret = nomalloc_init();
    printf("Step 3: nomalloc_init returned %d\n", ret);
    fflush(stdout);
    
    if (ret != 0) {
        printf("Step 4: nomalloc_init failed\n");
        fflush(stdout);
        return 1;
    }
    
    printf("Step 5.3: Calling aligned_alloc(128, 128)\n");
    fflush(stdout);
    void* ptr = aligned_alloc(128, 128);
    printf("Step 6.3: aligned_alloc returned %p\n", ptr);
    fflush(stdout);
    
    if (!ptr) {
        printf("Step 7.3: aligned_alloc failed\n");
        fflush(stdout);
        return 1;
    }
    
    uintptr_t addr = (uintptr_t)ptr;
    printf("Step 7.3: Checking alignment: addr=%p, addr%%128=%zu\n", ptr, addr % 128);
    fflush(stdout);
    
    if (addr % 128 != 0) {
        printf("Step 8.3: Alignment check FAILED\n");
        fflush(stdout);
        return 1;
    }
    
    printf("Step 9.3: Alignment check passed\n");
    fflush(stdout);
    
    memset(ptr, 0xAA, 128);
    
    printf("Step 12.3: Calling free\n");
    fflush(stdout);
    free(ptr);
    printf("Step 13.3: free done\n");
    fflush(stdout);
    
    printf("Step 14: Test passed!\n");
    fflush(stdout);
    return 0;
}