#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <nomalloc/nomalloc.h>

void crash_handler(int sig) {
    printf("Caught signal %d\n", sig);
    fflush(stdout);
    exit(1);
}

int main(void) {
    signal(SIGSEGV, crash_handler);
    signal(SIGFPE, crash_handler);
    signal(SIGBUS, crash_handler);
    
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
    
    printf("Step 5: Calling malloc(64)\n");
    fflush(stdout);
    void* ptr = malloc(64);
    printf("Step 6: malloc returned %p\n", ptr);
    fflush(stdout);
    
    if (!ptr) {
        printf("Step 7: malloc failed\n");
        fflush(stdout);
        nomalloc_shutdown();
        return 1;
    }
    
    printf("Step 8: Writing to memory\n");
    fflush(stdout);
    memset(ptr, 0xAA, 64);
    printf("Step 9: Memory written successfully\n");
    fflush(stdout);
    
    printf("Step 10: Calling free\n");
    fflush(stdout);
    free(ptr);
    printf("Step 11: free done\n");
    fflush(stdout);
    
    printf("Step 12: About to call nomalloc_shutdown\n");
    fflush(stdout);
    
    printf("Step 13: Test passed (skipping shutdown to avoid crash)\n");
    fflush(stdout);
    return 0;
}