#include "faar_alloc_hooks.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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

void faar_alloc_reset_counters(void) { calls = frees = 0; peak_bytes = live_bytes; }
void faar_alloc_fail_nth(size_t nth) { fail_at = nth ? calls + nth : 0; }
void faar_alloc_fail_all(int enabled) { fail_everything = enabled != 0; }
size_t faar_alloc_calls(void) { return calls; }
size_t faar_alloc_frees(void) { return frees; }
size_t faar_alloc_live(void) { return live_count; }
size_t faar_alloc_live_bytes(void) { return live_bytes; }
size_t faar_alloc_peak_bytes(void) { return peak_bytes; }
