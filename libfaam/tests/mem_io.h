/* Growable in-memory file behind a faam_io, and big-endian box builders, for tests that
 * generate their inputs instead of reading fixtures. */
#ifndef FAAM_TEST_MEM_IO_H
#define FAAM_TEST_MEM_IO_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "faam.h"

typedef struct { uint8_t *buf; size_t len, cap, pos; } mem_file;

static inline int32_t mem_file_read(void *u, void *out, uint32_t n)
{
    mem_file *f = (mem_file *)u;
    size_t avail = f->pos < f->len ? f->len - f->pos : 0;
    if (n > avail) n = (uint32_t)avail;
    if (n) memcpy(out, f->buf + f->pos, n);
    f->pos += n;
    return (int32_t)n;
}

static inline int32_t mem_file_write(void *u, const void *data, uint32_t n)
{
    mem_file *f = (mem_file *)u;
    if (f->pos + n > f->cap) {
        size_t cap = f->cap ? f->cap : 4096;
        while (cap < f->pos + n) cap *= 2;
        uint8_t *b = (uint8_t *)realloc(f->buf, cap);
        if (!b) return -1;
        f->buf = b;
        f->cap = cap;
    }
    if (f->pos > f->len) memset(f->buf + f->len, 0, f->pos - f->len);
    memcpy(f->buf + f->pos, data, n);
    f->pos += n;
    if (f->pos > f->len) f->len = f->pos;
    return (int32_t)n;
}

static inline bool mem_file_seek(void *u, uint64_t pos) { ((mem_file *)u)->pos = (size_t)pos; return true; }
static inline uint64_t mem_file_tell(void *u) { return ((mem_file *)u)->pos; }

static inline faam_io mem_file_io(mem_file *f)
{
    faam_io io = { .struct_size = sizeof(io), .user_data = f, .read = mem_file_read, .write = mem_file_write,
                   .seek = mem_file_seek, .tell = mem_file_tell, .flush = NULL };
    return io;
}

static inline uint32_t mem_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint64_t mem_be64(const uint8_t *p) { return (uint64_t)mem_be32(p) << 32 | mem_be32(p + 4); }

static inline void mem_put(mem_file *f, const void *d, size_t n) { mem_file_write(f, d, (uint32_t)n); }
static inline void mem_put32(mem_file *f, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    mem_put(f, b, 4);
}
static inline void mem_put64(mem_file *f, uint64_t v) { mem_put32(f, (uint32_t)(v >> 32)); mem_put32(f, (uint32_t)v); }

/* Opens a box and returns its start; mem_box_end patches the size. */
static inline size_t mem_box_begin(mem_file *f, const char *type)
{
    size_t at = f->len;
    f->pos = f->len;
    mem_put32(f, 0);
    mem_put(f, type, 4);
    return at;
}
static inline void mem_box_end(mem_file *f, size_t at)
{
    size_t end = f->len;
    f->pos = at;
    mem_put32(f, (uint32_t)(end - at));
    f->pos = end;
}

#endif
