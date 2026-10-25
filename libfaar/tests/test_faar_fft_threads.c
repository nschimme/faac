/* Concurrent initialization and private-workspace checks for FFT plans. */
#include "../fft_real.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FAAR_FFT_CACHE_BITS
#define FAAR_FFT_CACHE_BITS 16
#endif

#define WORKERS 8
#define STATIC_CASES 4
#define FALLBACK_N 131072
static const int sizes[STATIC_CASES] = {16, 256, 8192, 65536};

static int has_static_twiddle(int n) {
    int bits = 0;
    while (n > 1) { n >>= 1; ++bits; }
    return bits >= 4 && bits <= FAAR_FFT_CACHE_BITS;
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned waiting;
    unsigned generation;
    unsigned participants;
} test_barrier;

static int barrier_init(test_barrier *b, unsigned participants) {
    b->waiting = b->generation = 0;
    b->participants = participants;
    return pthread_mutex_init(&b->mutex, NULL) == 0 &&
           pthread_cond_init(&b->cond, NULL) == 0;
}

static void barrier_wait(test_barrier *b) {
    pthread_mutex_lock(&b->mutex);
    unsigned generation = b->generation;
    if (++b->waiting == b->participants) {
        b->waiting = 0;
        ++b->generation;
        pthread_cond_broadcast(&b->cond);
    } else {
        while (generation == b->generation)
            pthread_cond_wait(&b->cond, &b->mutex);
    }
    pthread_mutex_unlock(&b->mutex);
}

typedef struct {
    test_barrier *barrier;
    int id;
    int failed;
    faar_rfftf_plan fp[STATIC_CASES], ffallback, freopen;
    faar_rfftd_plan dp[STATIC_CASES], dfallback, dreopen;
} worker_t;

#define DEFINE_WORKER(TYPE, PLAN, PREFIX, MEMBER, FALLBACK, REOPEN, EPS) \
static int PREFIX##_roundtrip(PLAN *p) { \
    int n = p->size / 2; \
    TYPE *a = PREFIX##_cbuffer(p, 0); \
    for (int i = 0; i < n; ++i) { \
        a[i] = (TYPE)(0.7 * sin((double)i * 0.013) + 0.2 * cos((double)i * 0.031)); \
        a[n + i] = (TYPE)(0.3 * cos((double)i * 0.017)); \
    } \
    TYPE *freq = PREFIX##_cforward(p, a); \
    TYPE *conj_in = PREFIX##_cbuffer(p, freq == PREFIX##_cbuffer(p, 0) ? 1 : 0); \
    for (int i = 0; i < n; ++i) { conj_in[i] = freq[i]; conj_in[n + i] = -freq[n + i]; } \
    TYPE *back = PREFIX##_cforward(p, conj_in); \
    TYPE tolerance = (TYPE)(EPS); \
    for (int i = 0; i < n; ++i) { \
        TYPE real = back[i] / (TYPE)n; \
        TYPE imag = -back[n + i] / (TYPE)n; \
        TYPE original_real = (TYPE)(0.7 * sin((double)i * 0.013) + 0.2 * cos((double)i * 0.031)); \
        TYPE original_imag = (TYPE)(0.3 * cos((double)i * 0.017)); \
        if (!isfinite((double)real) || !isfinite((double)imag) || \
            (TYPE)fabs((double)(real - original_real)) > tolerance || \
            (TYPE)fabs((double)(imag - original_imag)) > tolerance) return 0; \
    } \
    return 1; \
} \
static void *PREFIX##_worker(void *opaque) { \
    worker_t *w = opaque; \
    barrier_wait(w->barrier); \
    for (int i = 0; i < STATIC_CASES; ++i) { \
        barrier_wait(w->barrier); \
        if (PREFIX##_cinit(&w->MEMBER[i], sizes[i]) != 0) w->failed = 1; \
    } \
    if (w->id < 2 && PREFIX##_cinit(&w->FALLBACK, FALLBACK_N) != 0) w->failed = 1; \
    barrier_wait(w->barrier); \
    barrier_wait(w->barrier); \
    for (int i = 0; i < STATIC_CASES; ++i) \
        if (w->MEMBER[i].work && !PREFIX##_roundtrip(&w->MEMBER[i])) w->failed = 1; \
    if (w->id < 2 && w->FALLBACK.work && !PREFIX##_roundtrip(&w->FALLBACK)) w->failed = 1; \
    for (int i = 0; i < STATIC_CASES; ++i) PREFIX##_clear(&w->MEMBER[i]); \
    if (w->id < 2) PREFIX##_clear(&w->FALLBACK); \
    if (PREFIX##_cinit(&w->REOPEN, sizes[1]) != 0) w->failed = 1; \
    barrier_wait(w->barrier); \
    barrier_wait(w->barrier); \
    PREFIX##_clear(&w->REOPEN); \
    return NULL; \
}

DEFINE_WORKER(float, faar_rfftf_plan, faar_rfftf, fp, ffallback, freopen, 2e-5)
DEFINE_WORKER(double, faar_rfftd_plan, faar_rfftd, dp, dfallback, dreopen, 2e-11)

#define DEFINE_CHECK(TYPE, PLAN, MEMBER, FALLBACK, REOPEN) \
static int check_##MEMBER(worker_t *workers) { \
    for (int k = 0; k < STATIC_CASES; ++k) { \
        for (int t = 0; t < WORKERS; ++t) { \
            PLAN *p = &workers[t].MEMBER[k]; \
            int cached = has_static_twiddle(sizes[k]); \
            if (!p->twiddle || p->twiddle_owned == cached || !p->work || !p->scratch) return 0; \
            for (int u = 0; u < t; ++u) { \
                if ((cached && p->twiddle != workers[u].MEMBER[k].twiddle) || \
                    (!cached && p->twiddle == workers[u].MEMBER[k].twiddle) || \
                    p->work == workers[u].MEMBER[k].work || \
                    p->scratch == workers[u].MEMBER[k].scratch) return 0; \
            } \
        } \
    } \
    for (int t = 0; t < 2; ++t) { \
        if (!workers[t].FALLBACK.twiddle_owned || \
            workers[t].FALLBACK.twiddle == workers[1-t].FALLBACK.twiddle) return 0; \
    } \
    return 1; \
}
DEFINE_CHECK(float, faar_rfftf_plan, fp, ffallback, freopen)
DEFINE_CHECK(double, faar_rfftd_plan, dp, dfallback, dreopen)

static int run_workers(worker_t *workers, test_barrier *barrier,
                       void *(*routine)(void *), int is_float) {
    pthread_t threads[WORKERS];
    int created = 0;
    for (int i = 0; i < WORKERS; ++i) {
        if (pthread_create(&threads[i], NULL, routine, &workers[i]) != 0)
            break;
        ++created;
    }
    /* Worker routines have fixed-count barriers. If thread creation fails,
     * do not release any worker into a barrier that can no longer complete. */
    if (created != WORKERS) {
        fprintf(stderr, "test_faar_fft_threads: could not create workers\n");
        exit(EXIT_FAILURE);
    }
    barrier_wait(barrier); /* release first-init race */
    for (int k = 0; k < STATIC_CASES; ++k)
        barrier_wait(barrier); /* race first initialization of this size */
    barrier_wait(barrier); /* all plans initialized and still live */
    int ok = is_float ? check_fp(workers) : check_dp(workers);
    int reopen_cached = has_static_twiddle(sizes[1]);
    void *cached = is_float ? (void *)workers[0].fp[1].twiddle
                            : (void *)workers[0].dp[1].twiddle;
    barrier_wait(barrier); /* permit transforms and clear/reopen */
    barrier_wait(barrier); /* reopened plans are live */
    for (int i = 0; i < WORKERS; ++i) {
        void *twiddle = is_float ? (void *)workers[i].freopen.twiddle
                                 : (void *)workers[i].dreopen.twiddle;
        int owned = is_float ? workers[i].freopen.twiddle_owned
                             : workers[i].dreopen.twiddle_owned;
        if (!twiddle || owned == reopen_cached ||
            (reopen_cached && twiddle != cached)) ok = 0;
        for (int u = 0; u < i && !reopen_cached; ++u) {
            void *other = is_float ? (void *)workers[u].freopen.twiddle
                                   : (void *)workers[u].dreopen.twiddle;
            if (twiddle == other) ok = 0;
        }
        if (workers[i].failed) ok = 0;
    }
    barrier_wait(barrier); /* permit cleanup */
    for (int i = 0; i < WORKERS; ++i) pthread_join(threads[i], NULL);
    return ok;
}

int main(void) {
    worker_t workers[WORKERS];
    test_barrier barrier;
    memset(workers, 0, sizeof(workers));
    if (!barrier_init(&barrier, WORKERS + 1)) {
        fprintf(stderr, "test_faar_fft_threads: barrier initialization failed\n");
        return 1;
    }
    for (int i = 0; i < WORKERS; ++i) {
        workers[i].barrier = &barrier;
        workers[i].id = i;
    }
    int ok = run_workers(workers, &barrier, faar_rfftf_worker, 1);
    memset(workers, 0, sizeof(workers));
    for (int i = 0; i < WORKERS; ++i) {
        workers[i].barrier = &barrier;
        workers[i].id = i;
    }
    if (!run_workers(workers, &barrier, faar_rfftd_worker, 0)) ok = 0;
    pthread_cond_destroy(&barrier.cond);
    pthread_mutex_destroy(&barrier.mutex);
    return ok ? 0 : 1;
}
