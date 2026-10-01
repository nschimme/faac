/*
 * Allocator hook for test_faam_alloc: routes every libfaam allocation through
 * counters so the test can assert that fragmented muxing allocates nothing.
 */

#ifndef FAAM_ALLOC_HOOK_H
#define FAAM_ALLOC_HOOK_H

#include <stddef.h>

void *faam_counted_alloc(size_t size);
void *faam_counted_realloc(void *block, size_t size);
void faam_counted_free(void *block);

#define AllocMemory(size) faam_counted_alloc(size)
#define AllocMemoryFast(size) faam_counted_alloc(size)
#define ReallocMemory(block, size) faam_counted_realloc(block, size)
#define FreeMemory(block) faam_counted_free(block)
#define FreeMemoryFast(block) faam_counted_free(block)

#endif
