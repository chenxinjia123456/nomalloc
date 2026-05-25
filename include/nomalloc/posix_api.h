#ifndef NOMALLOC_POSIX_API_H
#define NOMALLOC_POSIX_API_H

#include <stddef.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NOMALLOC_EXPORT __attribute__((visibility("default")))

NOMALLOC_EXPORT void* malloc(size_t size);
NOMALLOC_EXPORT void free(void* ptr);
NOMALLOC_EXPORT void* calloc(size_t nmemb, size_t size);
NOMALLOC_EXPORT void* realloc(void* ptr, size_t size);

NOMALLOC_EXPORT int posix_memalign(void** memptr, size_t alignment, size_t size);
NOMALLOC_EXPORT void* aligned_alloc(size_t alignment, size_t size);
NOMALLOC_EXPORT void* memalign(size_t alignment, size_t size);
NOMALLOC_EXPORT void* valloc(size_t size);
NOMALLOC_EXPORT void* pvalloc(size_t size);

NOMALLOC_EXPORT size_t malloc_usable_size(void* ptr);

NOMALLOC_EXPORT void* mallocx(size_t size, int flags);
NOMALLOC_EXPORT void dallocx(void* ptr, int flags);
NOMALLOC_EXPORT void sdallocx(void* ptr, size_t size, int flags);
NOMALLOC_EXPORT void* rallocx(void* ptr, size_t size, int flags);
NOMALLOC_EXPORT size_t xallocx(void* ptr, size_t size, size_t extra, int flags);
NOMALLOC_EXPORT size_t sallocx(void* ptr, int flags);
NOMALLOC_EXPORT size_t nallocx(size_t size, int flags);

#define MALLOCX_ALIGN(a) ((a) << 1)
#define MALLOCX_ZERO     ((int)0x40)
#define MALLOCX_TCACHE(tc) ((int)((tc) << 12))
#define MALLOCX_TCACHE_NONE MALLOCX_TCACHE(-1)

#ifdef __cplusplus
}
#endif

#endif