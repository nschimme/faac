#ifndef FAAR_TEST_ALLOC_HOOKS_H
#define FAAR_TEST_ALLOC_HOOKS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
void *faar_alloc_malloc(size_t size);
void faar_alloc_free(void *ptr);
void faar_alloc_reset_counters(void);
void faar_alloc_fail_nth(size_t nth);
void faar_alloc_fail_all(int enabled);
size_t faar_alloc_calls(void);
size_t faar_alloc_frees(void);
size_t faar_alloc_live(void);
size_t faar_alloc_live_bytes(void);
size_t faar_alloc_peak_bytes(void);
#ifdef __cplusplus
}
#endif

/* The library allocates only through these hooks. */
#define AllocMemory(size) faar_alloc_malloc(size)
#define FreeMemory(block) faar_alloc_free(block)

#endif
