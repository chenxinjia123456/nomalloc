#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nomalloc/nomalloc.h>

int main(void) {
    printf("Step 1: Init\n");
    fflush(stdout);
    nomalloc_init();
    
    printf("Step 2: malloc(100)\n");
    fflush(stdout);
    void* ptr1 = malloc(100);
    printf("Step 3: ptr1 = %p\n", ptr1);
    fflush(stdout);
    
    if (!ptr1) {
        printf("FAIL: malloc returned NULL\n");
        return 1;
    }
    
    printf("Step 4: memset\n");
    fflush(stdout);
    memset(ptr1, 0xAA, 100);
    
    printf("Step 5: free\n");
    fflush(stdout);
    free(ptr1);
    printf("Step 6: free done\n");
    fflush(stdout);
    
    printf("Step 7: malloc(1000)\n");
    fflush(stdout);
    void* ptr2 = malloc(1000);
    printf("Step 8: ptr2 = %p\n", ptr2);
    fflush(stdout);
    
    if (!ptr2) {
        printf("FAIL: malloc returned NULL\n");
        return 1;
    }
    
    printf("Step 9: memset\n");
    fflush(stdout);
    memset(ptr2, 0xBB, 1000);
    
    printf("Step 10: free\n");
    fflush(stdout);
    free(ptr2);
    printf("Step 11: free done\n");
    fflush(stdout);
    
    printf("PASS!\n");
    fflush(stdout);
    return 0;
}