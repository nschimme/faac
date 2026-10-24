#ifndef FAAR_TEST_ALLOC_HOOKS_H
#define FAAR_TEST_ALLOC_HOOKS_H

/* Include allocator declarations before interposing calls in library sources. */
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <malloc.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
void *faar_alloc_malloc(size_t size);
void *faar_alloc_calloc(size_t count, size_t size);
void *faar_alloc_realloc(void *ptr, size_t size);
void faar_alloc_free(void *ptr);
int faar_alloc_posix_memalign(void **ptr, size_t alignment, size_t size);
#if defined(_WIN32)
void *faar_alloc_aligned_malloc(size_t size, size_t alignment);
void faar_alloc_aligned_free(void *ptr);
#endif
void faar_alloc_reset_counters(void);
void faar_alloc_fail_nth(size_t nth);
void faar_alloc_fail_all(int enabled);
size_t faar_alloc_calls(void);
size_t faar_alloc_frees(void);
size_t faar_alloc_live(void);
#ifdef __cplusplus
}
#endif

#define malloc faar_alloc_malloc
#define calloc faar_alloc_calloc
#define realloc faar_alloc_realloc
#define free faar_alloc_free
#if !defined(_WIN32)
#define posix_memalign faar_alloc_posix_memalign
#else
#define _aligned_malloc faar_alloc_aligned_malloc
#define _aligned_free faar_alloc_aligned_free
#endif

#endif
