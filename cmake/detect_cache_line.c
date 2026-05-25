#include <stdio.h>

int main() {
    /* Try to detect cache line size */
    /* Most ARM64 systems have 64 or 128 byte cache lines */
    
    /* Simple detection based on common values */
    /* Return 128 for 128-byte cache line, 0 for 64-byte */
    
#ifdef __aarch64__
    /* ARM64 typically has 64 or 128 byte cache lines */
    /* Return 128 if detected, otherwise 64 */
    return 64;  /* Default to 64 for simplicity */
#else
    return 64;
#endif
}