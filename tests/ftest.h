/* Minimal test checks. FT_CHECK records a failure and keeps going; FT_REQUIRE stops
 * the test, for preconditions the rest of the test would crash without.
 * Never uses assert(), so a build defining NDEBUG cannot disable it. */
#ifndef FTEST_H
#define FTEST_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FT_SKIP_STATUS 77 /* meson's "skipped" exit code */

static int ft_failures;

#define FT_CHECK(cond) \
    do { if (!(cond)) { ft_failures++; fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define FT_REQUIRE(cond) \
    do { if (!(cond)) { fprintf(stderr, "%s:%d: REQUIRE %s\n", __FILE__, __LINE__, #cond); exit(1); } } while (0)

#define FT_CHECK_INT(got, want) \
    do { long long ft_g = (long long)(got), ft_w = (long long)(want); \
         if (ft_g != ft_w) { ft_failures++; \
             fprintf(stderr, "%s:%d: FAIL %s == %s (got %lld, want %lld)\n", __FILE__, __LINE__, #got, #want, ft_g, ft_w); } } while (0)

#define FT_CHECK_NEAR(got, want, tol) \
    do { double ft_g = (double)(got), ft_w = (double)(want), ft_t = (double)(tol); \
         if (!(ft_g >= ft_w - ft_t && ft_g <= ft_w + ft_t)) { ft_failures++; \
             fprintf(stderr, "%s:%d: FAIL %s ~ %s (got %.9g, want %.9g +/- %.9g)\n", __FILE__, __LINE__, #got, #want, ft_g, ft_w, ft_t); } } while (0)

/* Reports the first differing offset, not just that the buffers differ. */
#define FT_CHECK_MEM(got, want, n) \
    do { const uint8_t *ft_a = (const uint8_t *)(got), *ft_b = (const uint8_t *)(want); size_t ft_n = (n), ft_i = 0; \
         while (ft_i < ft_n && ft_a[ft_i] == ft_b[ft_i]) ft_i++; \
         if (ft_i < ft_n) { ft_failures++; \
             fprintf(stderr, "%s:%d: FAIL %s == %s differ at byte %zu of %zu (0x%02x vs 0x%02x)\n", __FILE__, __LINE__, #got, #want, ft_i, ft_n, ft_a[ft_i], ft_b[ft_i]); } } while (0)

static inline int ft_done(void) { return ft_failures ? 1 : 0; }

/* xorshift32: a fixed seed makes every run identical. */
static inline uint32_t ft_rand(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}

#endif
