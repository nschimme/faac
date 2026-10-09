/*
 * FAAM - Freeware Advanced Audio/Video Muxer
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

/*
 * Allocator hook for test_faam_alloc, force-included into libfaam's sources:
 * routes every libfaam allocation through counters so the test can assert
 * that fragmented muxing allocates nothing after open and failures do not leak.
 */

#ifndef TEST_FAAM_ALLOC_H
#define TEST_FAAM_ALLOC_H

#include <stddef.h>

void *faam_counted_alloc(size_t size);
void faam_counted_free(void *block);

#define AllocMemory(size) faam_counted_alloc(size)
#define FreeMemory(block) faam_counted_free(block)

#endif
