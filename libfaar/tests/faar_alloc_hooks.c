#include "faar_alloc_hooks.h"

/* This translation unit must call the process allocator directly. */
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef posix_memalign
#ifdef _WIN32
#undef _aligned_malloc
#undef _aligned_free
#endif

#include <stdint.h>
#include <string.h>
#if !defined(_WIN32)
extern int posix_memalign(void **ptr, size_t alignment, size_t size);
#endif

typedef struct allocation_record {
    void *ptr;
    size_t bytes;
    struct allocation_record *next;
} allocation_record;

static allocation_record *records;
static size_t calls, frees, live_count, live_bytes, peak_bytes, fail_at;
static int fail_everything;

static int should_fail(void) {
    ++calls;
    return fail_everything || (fail_at && calls == fail_at);
}

static allocation_record **find_record(void *ptr) {
    allocation_record **link = &records;
    while (*link && (*link)->ptr != ptr) link = &(*link)->next;
    return link;
}

static int remember(void *ptr, size_t bytes) {
    allocation_record *record;
    if (!ptr) return 0;
    record = malloc(sizeof(*record));
    if (!record) return 0;
    record->ptr = ptr;
    record->bytes = bytes;
    record->next = records;
    records = record;
    ++live_count;
    live_bytes += bytes;
    if (live_bytes > peak_bytes) peak_bytes = live_bytes;
    return 1;
}

void *faar_alloc_malloc(size_t size) {
    if (should_fail()) return NULL;
    void *ptr = malloc(size);
    if (ptr && !remember(ptr, size)) { free(ptr); return NULL; }
    return ptr;
}

void *faar_alloc_calloc(size_t count, size_t size) {
    if (should_fail()) return NULL;
    void *ptr = calloc(count, size);
    size_t bytes = size && count > SIZE_MAX / size ? SIZE_MAX : count * size;
    if (ptr && !remember(ptr, bytes)) { free(ptr); return NULL; }
    return ptr;
}

void *faar_alloc_realloc(void *ptr, size_t size) {
    if (should_fail()) return NULL;
    allocation_record **link = ptr ? find_record(ptr) : NULL;
    void *next = realloc(ptr, size);
    if (!next && size) return NULL;
    if (!ptr) {
        if (next && !remember(next, size)) { free(next); return NULL; }
    } else if (link && *link) {
        if (!next) {
            allocation_record *old = *link;
            *link = old->next;
            if (live_bytes >= old->bytes) live_bytes -= old->bytes;
            free(old);
            if (live_count) --live_count;
        } else {
            size_t old_bytes = (*link)->bytes;
            if (live_bytes >= old_bytes) live_bytes -= old_bytes;
            live_bytes += size;
            (*link)->bytes = size;
            (*link)->ptr = next;
            if (live_bytes > peak_bytes) peak_bytes = live_bytes;
        }
    } else if (next && !remember(next, size)) {
        free(next);
        return NULL;
    }
    return next;
}

void faar_alloc_free(void *ptr) {
    if (!ptr) return;
    ++frees;
    allocation_record **link = find_record(ptr);
    if (*link) {
        allocation_record *old = *link;
        *link = old->next;
        if (live_bytes >= old->bytes) live_bytes -= old->bytes;
        free(old);
        if (live_count) --live_count;
    }
    free(ptr);
}

#ifdef _WIN32
static void forget_allocation(void *ptr) {
    allocation_record **link = find_record(ptr);
    if (*link) {
        allocation_record *old = *link;
        *link = old->next;
        if (live_bytes >= old->bytes) live_bytes -= old->bytes;
        free(old);
        if (live_count) --live_count;
    }
    ++frees;
}
#endif

#if !defined(_WIN32)
int faar_alloc_posix_memalign(void **ptr, size_t alignment, size_t size) {
    if (should_fail()) return ENOMEM;
    int status = posix_memalign(ptr, alignment, size);
    if (!status && *ptr && !remember(*ptr, size)) { free(*ptr); *ptr = NULL; return ENOMEM; }
    return status;
}
#endif

#ifdef _WIN32
void *faar_alloc_aligned_malloc(size_t size, size_t alignment) {
    if (should_fail()) return NULL;
    void *ptr = _aligned_malloc(size, alignment);
    if (ptr && !remember(ptr, size)) { _aligned_free(ptr); return NULL; }
    return ptr;
}
void faar_alloc_aligned_free(void *ptr) {
    if (!ptr) return;
    forget_allocation(ptr);
    _aligned_free(ptr);
}
#endif

void faar_alloc_reset_counters(void) { calls = frees = 0; peak_bytes = live_bytes; }
void faar_alloc_fail_nth(size_t nth) { fail_at = nth ? calls + nth : 0; }
void faar_alloc_fail_all(int enabled) { fail_everything = enabled != 0; }
size_t faar_alloc_calls(void) { return calls; }
size_t faar_alloc_frees(void) { return frees; }
size_t faar_alloc_live(void) { return live_count; }
size_t faar_alloc_live_bytes(void) { return live_bytes; }
size_t faar_alloc_peak_bytes(void) { return peak_bytes; }
