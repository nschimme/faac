#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

#ifndef M_PI_DOUBLE
#define M_PI_DOUBLE 3.14159265358979323846
#endif

#define FFT_MAXLOGM 9
#define BLOCK_LEN_LONG 1024
#define BLOCK_LEN_SHORT 128

typedef float fftfloat;

typedef struct
{
    fftfloat **costbl;
    fftfloat **negsintbl;
    unsigned short **reordertbl;
    fftfloat *mdct_cos[FFT_MAXLOGM + 1];
    fftfloat *mdct_sin[FFT_MAXLOGM + 1];
} FFT_Tables;

/* Memory allocation helpers matching the standard */
void *AllocMemory(size_t size) {
    return malloc(size);
}
void FreeMemory(void *ptr) {
    free(ptr);
}

void fft_initialize(FFT_Tables *fft_tables)
{
    int i;
    fft_tables->costbl = AllocMemory((FFT_MAXLOGM + 1) * sizeof(fft_tables->costbl[0]));
    fft_tables->negsintbl = AllocMemory((FFT_MAXLOGM + 1) * sizeof(fft_tables->negsintbl[0]));
    fft_tables->reordertbl = AllocMemory((FFT_MAXLOGM + 1) * sizeof(fft_tables->reordertbl[0]));

    for (i = 0; i < FFT_MAXLOGM + 1; i++)
    {
        fft_tables->costbl[i] = NULL;
        fft_tables->negsintbl[i] = NULL;
        fft_tables->reordertbl[i] = NULL;
    }

    for (i = 0; i < FFT_MAXLOGM + 1; i++)
    {
        fft_tables->mdct_cos[i] = NULL;
        fft_tables->mdct_sin[i] = NULL;
    }

    static const int logms[2] = { 6, 9 };
    int t;
    for (t = 0; t < 2; t++)
    {
        int logm = logms[t];
        int size = 1 << logm;
        double freq = 2.0 * M_PI_DOUBLE / (double)(4 << logm);
        fftfloat *c = AllocMemory(size * sizeof(fftfloat));
        fftfloat *s = AllocMemory(size * sizeof(fftfloat));

        for (i = 0; i < size; i++)
        {
            double theta = freq * ((double)i + 0.125);
            c[i] = (fftfloat)cos(theta);
            s[i] = (fftfloat)sin(theta);
        }
        fft_tables->mdct_cos[logm] = c;
        fft_tables->mdct_sin[logm] = s;
    }
}

void fft_terminate(FFT_Tables *fft_tables)
{
    int i;
    for (i = 0; i < FFT_MAXLOGM + 1; i++)
    {
        if (fft_tables->costbl[i] != NULL) FreeMemory(fft_tables->costbl[i]);
        if (fft_tables->negsintbl[i] != NULL) FreeMemory(fft_tables->negsintbl[i]);
        if (fft_tables->reordertbl[i] != NULL) FreeMemory(fft_tables->reordertbl[i]);
    }
    for (i = 0; i < FFT_MAXLOGM + 1; i++)
    {
        if (fft_tables->mdct_cos[i] != NULL) FreeMemory(fft_tables->mdct_cos[i]);
        if (fft_tables->mdct_sin[i] != NULL) FreeMemory(fft_tables->mdct_sin[i]);
    }
    FreeMemory(fft_tables->costbl);
    FreeMemory(fft_tables->negsintbl);
    FreeMemory(fft_tables->reordertbl);
}

static void check_tables_radix4(FFT_Tables *fft_tables, int logm)
{
    if (fft_tables->costbl[logm] == NULL)
    {
        int size = 1 << logm;
        int i;
        fft_tables->costbl[logm] = AllocMemory(size * sizeof(*(fft_tables->costbl[0])));
        fft_tables->negsintbl[logm] = AllocMemory(size * sizeof(*(fft_tables->negsintbl[0])));

        for (i = 0; i < size; i++)
        {
            double theta = 2.0 * M_PI_DOUBLE * (double)i / (double)size;
            fft_tables->costbl[logm][i] = (fftfloat)cos(theta);
            fft_tables->negsintbl[logm][i] = (fftfloat)-sin(theta);
        }
    }
}

static void check_reorder_table(FFT_Tables *fft_tables, int logm)
{
    if (fft_tables->reordertbl[logm] == NULL)
    {
        int size = 1 << logm;
        int i;
        fft_tables->reordertbl[logm] = AllocMemory(size * sizeof(*(fft_tables->reordertbl[0])));

        for (i = 0; i < size; i++)
        {
            int reversed = 0;
            int b;
            int tmp = i;
            for (b = 0; b < logm; b++)
            {
                reversed = (reversed << 1) | (tmp & 1);
                tmp >>= 1;
            }
            fft_tables->reordertbl[logm][i] = (unsigned short)reversed;
        }
    }
}

/* Reference Implementation */
static void reference_radix4_dif_proc(
    float * restrict xr,
    float * restrict xi,
    int logm,
    const fftfloat * restrict costbl,
    const fftfloat * restrict sintbl)
{
    int n = 1 << logm;
    int n2 = n;
    int n1;
    int i, j, k;

    for (k = 0; k < (logm >> 1); k++)
    {
        n1 = n2;
        n2 >>= 2;
        for (i = 0; i < n; i += n1)
        {
            float * restrict r1p = xr + i;
            float * restrict r2p = xr + i + n2;
            float * restrict r3p = xr + i + 2*n2;
            float * restrict r4p = xr + i + 3*n2;
            float * restrict i1p = xi + i;
            float * restrict i2p = xi + i + n2;
            float * restrict i3p = xi + i + 2*n2;
            float * restrict i4p = xi + i + 3*n2;

            {
                float r1 = *r1p, i1 = *i1p;
                float r2 = *r2p, i2 = *i2p;
                float r3 = *r3p, i3 = *i3p;
                float r4 = *r4p, i4 = *i4p;

                float t1 = r1 + r3, t2 = i1 + i3;
                float t3 = r2 + r4, t4 = i2 + i4;
                float t5 = r1 - r3, t6 = i1 - i3;
                float t7 = r2 - r4, t8 = i2 - i4;

                *r1p = t1 + t3; *i1p = t2 + t4;
                *r3p = t5 + t8; *i3p = t6 - t7;
                *r2p = t1 - t3; *i2p = t2 - t4;
                *r4p = t5 - t8; *i4p = t6 + t7;

                r1p++; r2p++; r3p++; r4p++;
                i1p++; i2p++; i3p++; i4p++;
            }

            for (j = 1; j < n2; j++)
            {
                int tw_idx = j << (2 * k);
                const float c1 = (float)costbl[tw_idx];
                const float s1 = (float)sintbl[tw_idx];
                const float c2 = (float)costbl[2 * tw_idx];
                const float s2 = (float)sintbl[2 * tw_idx];
                const float c3 = (float)costbl[3 * tw_idx];
                const float s3 = (float)sintbl[3 * tw_idx];

                float r1 = *r1p, i1 = *i1p;
                float r2 = *r2p, i2 = *i2p;
                float r3 = *r3p, i3 = *i3p;
                float r4 = *r4p, i4 = *i4p;

                float t1 = r1 + r3, t2 = i1 + i3;
                float t3 = r2 + r4, t4 = i2 + i4;
                float t5 = r1 - r3, t6 = i1 - i3;
                float t7 = r2 - r4, t8 = i2 - i4;

                *r1p = t1 + t3;
                *i1p = t2 + t4;

                r1 = t1 - t3; i1 = t2 - t4;
                r2 = t5 + t8; i2 = t6 - t7;
                r3 = t5 - t8; i3 = t6 + t7;

                *r3p = r2 * c1 - i2 * s1;
                *i3p = r2 * s1 + i2 * c1;
                *r2p = r1 * c2 - i1 * s2;
                *i2p = r1 * s2 + i1 * c2;
                *r4p = r3 * c3 - i3 * s3;
                *i4p = r3 * s3 + i3 * c3;

                r1p++; r2p++; r3p++; r4p++;
                i1p++; i2p++; i3p++; i4p++;
            }
        }
    }

    if (logm & 1)
    {
        float * restrict r1p = xr;
        float * restrict r2p = xr + 1;
        float * restrict i1p = xi;
        float * restrict i2p = xi + 1;
        for (i = 0; i < n; i += 2)
        {
            float r1 = *r1p, i1 = *i1p;
            float r2 = *r2p, i2 = *i2p;
            *r1p = r1 + r2; *i1p = i1 + i2;
            *r2p = r1 - r2; *i2p = i1 - i2;
            r1p += 2; r2p += 2; i1p += 2; i2p += 2;
        }
    }
}

static void reference_bit_reverse(
    float * restrict xr,
    float * restrict xi,
    int logm,
    const unsigned short * restrict r)
{
    int i;
    int size = 1 << logm;

    for (i = 0; i < size; i++)
    {
        int j = (int)r[i];
        if (j > i)
        {
            float tr = xr[i]; xr[i] = xr[j]; xr[j] = tr;
            float ti = xi[i]; xi[i] = xi[j]; xi[j] = ti;
        }
    }
}

void reference_MDCT(FFT_Tables *fft_tables, float * restrict data, int N, float * restrict work)
{
    const int N2 = N >> 1;
    const int N4 = N >> 2;
    const int N8 = N >> 3;
    const int logm = (N == 2 * BLOCK_LEN_LONG) ? 9 : 6;

    check_tables_radix4(fft_tables, logm);
    check_reorder_table(fft_tables, logm);

    const fftfloat * restrict cosT = fft_tables->mdct_cos[logm];
    const fftfloat * restrict sinT = fft_tables->mdct_sin[logm];

    float * restrict xr = work;
    float * restrict xi = work + N4;

    int i;

    for (i = 0; i < N8; i++) {
        int n1 = N2 - 1 - 2*i;
        int n2 = 2*i;
        float foldedRe = data[N4 + n1] + data[N + N4 - 1 - n1];
        float foldedIm = data[N4 + n2] - data[N4 - 1 - n2];

        xr[i] = foldedRe * cosT[i] + foldedIm * sinT[i];
        xi[i] = foldedIm * cosT[i] - foldedRe * sinT[i];
    }
    for (; i < N4; i++) {
        int n1 = N2 - 1 - 2*i;
        int n2 = 2*i;
        float foldedRe = data[N4 + n1] - data[N4 - 1 - n1];
        float foldedIm = data[N4 + n2] + data[N + N4 - 1 - n2];

        xr[i] = foldedRe * cosT[i] + foldedIm * sinT[i];
        xi[i] = foldedIm * cosT[i] - foldedRe * sinT[i];
    }

    reference_radix4_dif_proc(xr, xi, logm, fft_tables->costbl[logm], fft_tables->negsintbl[logm]);
    reference_bit_reverse(xr, xi, logm, fft_tables->reordertbl[logm]);

    for (i = 0; i < N4; i++) {
        int n2 = 2*i;
        float unfoldRe = 2.0f * (xr[i] * cosT[i] + xi[i] * sinT[i]);
        float unfoldIm = 2.0f * (xi[i] * cosT[i] - xr[i] * sinT[i]);

        data[n2]             = -unfoldRe;
        data[N2 - 1 - n2]    =  unfoldIm;
        data[N2 + n2]        = -unfoldIm;
        data[N - 1 - n2]     =  unfoldRe;
    }
}


/* Optimized Implementation (Loop Fused & Bit-Reversal Free) */

/* Pre-twiddle fused directly into the first stage of Radix-4 DIF */
static void fused_pretwiddle_stage0(
    const float * restrict data,
    int N,
    float * restrict xr,
    float * restrict xi,
    const fftfloat * restrict cosT,
    const fftfloat * restrict sinT,
    const fftfloat * restrict costbl,
    const fftfloat * restrict sintbl,
    int logm)
{
    const int N2 = N >> 1;
    const int N4 = N >> 2;
    const int N8 = N >> 3;
    const int n2 = N4 >> 2; /* stage 0: n2 = N4 / 4 */

    int j;

    /* Loop over j. Inside we compute pre-twiddles for:
     * j, j + n2, j + 2*n2, j + 3*n2
     */
    for (j = 0; j < n2; j++) {
        /* Determine values for indices:
         * idx1 = j, idx2 = j + n2, idx3 = j + 2*n2, idx4 = j + 3*n2
         */
        int idx1 = j;
        int idx2 = j + n2;
        int idx3 = j + 2 * n2;
        int idx4 = j + 3 * n2;

        /* For idx1 and idx2, they are < N8 (since j < n2 => j + n2 < 2n2 = N8) */
        /* idx1 */
        int n1_1 = N2 - 1 - 2*idx1;
        int n2_1 = 2*idx1;
        float fRe1 = data[N4 + n1_1] + data[N + N4 - 1 - n1_1];
        float fIm1 = data[N4 + n2_1] - data[N4 - 1 - n2_1];
        float r1 = fRe1 * cosT[idx1] + fIm1 * sinT[idx1];
        float i1 = fIm1 * cosT[idx1] - fRe1 * sinT[idx1];

        /* idx2 */
        int n1_2 = N2 - 1 - 2*idx2;
        int n2_2 = 2*idx2;
        float fRe2 = data[N4 + n1_2] + data[N + N4 - 1 - n1_2];
        float fIm2 = data[N4 + n2_2] - data[N4 - 1 - n2_2];
        float r2 = fRe2 * cosT[idx2] + fIm2 * sinT[idx2];
        float i2 = fIm2 * cosT[idx2] - fRe2 * sinT[idx2];

        /* For idx3 and idx4, they are >= N8 */
        /* idx3 */
        int n1_3 = N2 - 1 - 2*idx3;
        int n2_3 = 2*idx3;
        float fRe3 = data[N4 + n1_3] - data[N4 - 1 - n1_3];
        float fIm3 = data[N4 + n2_3] + data[N + N4 - 1 - n2_3];
        float r3 = fRe3 * cosT[idx3] + fIm3 * sinT[idx3];
        float i3 = fIm3 * cosT[idx3] - fRe3 * sinT[idx3];

        /* idx4 */
        int n1_4 = N2 - 1 - 2*idx4;
        int n2_4 = 2*idx4;
        float fRe4 = data[N4 + n1_4] - data[N4 - 1 - n1_4];
        float fIm4 = data[N4 + n2_4] + data[N + N4 - 1 - n2_4];
        float r4 = fRe4 * cosT[idx4] + fIm4 * sinT[idx4];
        float i4 = fIm4 * cosT[idx4] - fRe4 * sinT[idx4];

        /* Now perform Radix-4 DIF Stage 0 butterfly */
        float t1 = r1 + r3, t2 = i1 + i3;
        float t3 = r2 + r4, t4 = i2 + i4;
        float t5 = r1 - r3, t6 = i1 - i3;
        float t7 = r2 - r4, t8 = i2 - i4;

        /* Write stage 0 outputs to xr/xi */
        if (j == 0) {
            xr[idx1] = t1 + t3; xi[idx1] = t2 + t4;
            xr[idx3] = t5 + t8; xi[idx3] = t6 - t7;
            xr[idx2] = t1 - t3; xi[idx2] = t2 - t4;
            xr[idx4] = t5 - t8; xi[idx4] = t6 + t7;
        } else {
            int tw_idx = j; /* for k = 0, j << (2*k) is just j */
            const float c1 = (float)costbl[tw_idx];
            const float s1 = (float)sintbl[tw_idx];
            const float c2 = (float)costbl[2 * tw_idx];
            const float s2 = (float)sintbl[2 * tw_idx];
            const float c3 = (float)costbl[3 * tw_idx];
            const float s3 = (float)sintbl[3 * tw_idx];

            xr[idx1] = t1 + t3;
            xi[idx1] = t2 + t4;

            float r1_out = t1 - t3; float i1_out = t2 - t4;
            float r2_out = t5 + t8; float i2_out = t6 - t7;
            float r3_out = t5 - t8; float i3_out = t6 + t7;

            xr[idx3] = r2_out * c1 - i2_out * s1;
            xi[idx3] = r2_out * s1 + i2_out * c1;
            xr[idx2] = r1_out * c2 - i1_out * s2;
            xi[idx2] = r1_out * s2 + i1_out * c2;
            xr[idx4] = r3_out * c3 - i3_out * s3;
            xi[idx4] = r3_out * s3 + i3_out * c3;
        }
    }
}

/* Rest of Radix-4 DIF (starting at k = 1) */
static void optimized_radix4_dif_proc_k1(
    float * restrict xr,
    float * restrict xi,
    int logm,
    const fftfloat * restrict costbl,
    const fftfloat * restrict sintbl)
{
    int n = 1 << logm;
    int n2 = n >> 2; /* stage 0 is already done, so n2 was shifted once */
    int n1;
    int i, j, k;

    /* Loop starts at k = 1 */
    for (k = 1; k < (logm >> 1); k++)
    {
        n1 = n2;
        n2 >>= 2;
        for (i = 0; i < n; i += n1)
        {
            float * restrict r1p = xr + i;
            float * restrict r2p = xr + i + n2;
            float * restrict r3p = xr + i + 2*n2;
            float * restrict r4p = xr + i + 3*n2;
            float * restrict i1p = xi + i;
            float * restrict i2p = xi + i + n2;
            float * restrict i3p = xi + i + 2*n2;
            float * restrict i4p = xi + i + 3*n2;

            /* j=0 unrolled */
            {
                float r1 = *r1p, i1 = *i1p;
                float r2 = *r2p, i2 = *i2p;
                float r3 = *r3p, i3 = *i3p;
                float r4 = *r4p, i4 = *i4p;

                float t1 = r1 + r3, t2 = i1 + i3;
                float t3 = r2 + r4, t4 = i2 + i4;
                float t5 = r1 - r3, t6 = i1 - i3;
                float t7 = r2 - r4, t8 = i2 - i4;

                *r1p = t1 + t3; *i1p = t2 + t4;
                *r3p = t5 + t8; *i3p = t6 - t7;
                *r2p = t1 - t3; *i2p = t2 - t4;
                *r4p = t5 - t8; *i4p = t6 + t7;

                r1p++; r2p++; r3p++; r4p++;
                i1p++; i2p++; i3p++; i4p++;
            }

            for (j = 1; j < n2; j++)
            {
                int tw_idx = j << (2 * k);
                const float c1 = (float)costbl[tw_idx];
                const float s1 = (float)sintbl[tw_idx];
                const float c2 = (float)costbl[2 * tw_idx];
                const float s2 = (float)sintbl[2 * tw_idx];
                const float c3 = (float)costbl[3 * tw_idx];
                const float s3 = (float)sintbl[3 * tw_idx];

                float r1 = *r1p, i1 = *i1p;
                float r2 = *r2p, i2 = *i2p;
                float r3 = *r3p, i3 = *i3p;
                float r4 = *r4p, i4 = *i4p;

                float t1 = r1 + r3, t2 = i1 + i3;
                float t3 = r2 + r4, t4 = i2 + i4;
                float t5 = r1 - r3, t6 = i1 - i3;
                float t7 = r2 - r4, t8 = i2 - i4;

                *r1p = t1 + t3;
                *i1p = t2 + t4;

                r1 = t1 - t3; i1 = t2 - t4;
                r2 = t5 + t8; i2 = t6 - t7;
                r3 = t5 - t8; i3 = t6 + t7;

                *r3p = r2 * c1 - i2 * s1;
                *i3p = r2 * s1 + i2 * c1;
                *r2p = r1 * c2 - i1 * s2;
                *i2p = r1 * s2 + i1 * c2;
                *r4p = r3 * c3 - i3 * s3;
                *i4p = r3 * s3 + i3 * c3;

                r1p++; r2p++; r3p++; r4p++;
                i1p++; i2p++; i3p++; i4p++;
            }
        }
    }

    if (logm & 1)
    {
        float * restrict r1p = xr;
        float * restrict r2p = xr + 1;
        float * restrict i1p = xi;
        float * restrict i2p = xi + 1;
        for (i = 0; i < n; i += 2)
        {
            float r1 = *r1p, i1 = *i1p;
            float r2 = *r2p, i2 = *i2p;
            *r1p = r1 + r2; *i1p = i1 + i2;
            *r2p = r1 - r2; *i2p = i1 - i2;
            r1p += 2; r2p += 2; i1p += 2; i2p += 2;
        }
    }
}

/* Optimized MDCT including loop fusion and bit-reversal elimination (Option A) */
void optimized_MDCT(FFT_Tables *fft_tables, float * restrict data, int N, float * restrict work)
{
    const int N2 = N >> 1;
    const int N4 = N >> 2;
    const int logm = (N == 2 * BLOCK_LEN_LONG) ? 9 : 6;

    check_tables_radix4(fft_tables, logm);
    check_reorder_table(fft_tables, logm);

    const fftfloat * restrict cosT = fft_tables->mdct_cos[logm];
    const fftfloat * restrict sinT = fft_tables->mdct_sin[logm];

    float * restrict xr = work;
    float * restrict xi = work + N4;

    /* 1. Fuse pre-twiddle directly into stage 0 of Radix-4 DIF.
     * This avoids writing xr/xi to scratch buffer and reading them back.
     */
    fused_pretwiddle_stage0(data, N, xr, xi, cosT, sinT, fft_tables->costbl[logm], fft_tables->negsintbl[logm], logm);

    /* 2. Run the remaining stages of the Radix-4 DIF FFT starting from k = 1 */
    optimized_radix4_dif_proc_k1(xr, xi, logm, fft_tables->costbl[logm], fft_tables->negsintbl[logm]);

    /* 3. We skip the reference_bit_reverse completely.
     * Instead, we fuse it directly into the post-twiddle/unfolding step
     * by performing bit-reversed lookups while writing to the linear destination.
     */
    const unsigned short * restrict reorder = fft_tables->reordertbl[logm];
    int i;
    for (i = 0; i < N4; i++) {
        int rev_i = (int)reorder[i];
        float r = xr[rev_i];
        float j = xi[rev_i];

        float unfoldRe = 2.0f * (r * cosT[i] + j * sinT[i]);
        float unfoldIm = 2.0f * (j * cosT[i] - r * sinT[i]);

        int n2 = 2 * i;
        data[n2]             = -unfoldRe;
        data[N2 - 1 - n2]    =  unfoldIm;
        data[N2 + n2]        = -unfoldIm;
        data[N - 1 - n2]     =  unfoldRe;
    }
}


/* Validation and Benchmarking Harness */

double calculate_snr(const float *ref, const float *cand, int size) {
    double signal_power = 0.0;
    double noise_power = 0.0;
    int i;

    for (i = 0; i < size; i++) {
        double r = (double)ref[i];
        double c = (double)cand[i];
        double diff = r - c;
        signal_power += r * r;
        noise_power += diff * diff;
    }

    if (signal_power == 0.0) {
        if (noise_power == 0.0) return 300.0; /* Perfect zero signals */
        return -300.0;
    }
    if (noise_power == 0.0) return 300.0; /* Perfect identity */

    return 10.0 * log10(signal_power / noise_power);
}

int main() {
    FFT_Tables fft_tables;
    fft_initialize(&fft_tables);

    printf("==================================================================\n");
    printf("         FAAC MDCT/FFT OPTIMIZATION PATH VERIFICATION TOOL\n");
    printf("==================================================================\n\n");

    /* Test sizes */
    int sizes[2] = { 2 * BLOCK_LEN_LONG, 2 * BLOCK_LEN_SHORT };
    const char *names[2] = { "Long Block (2048 samples)", "Short Block (256 samples)" };

    int s;
    for (s = 0; s < 2; s++) {
        int N = sizes[s];
        printf("--- Testing %s ---\n", names[s]);

        /* Allocate test arrays */
        float *data_ref = malloc(N * sizeof(float));
        float *data_opt = malloc(N * sizeof(float));
        float *work_ref = malloc(N * sizeof(float));
        float *work_opt = malloc(N * sizeof(float));

        /* Initialize input data with various signals */
        int i;
        srand(42);
        for (i = 0; i < N; i++) {
            /* Mixture of sine wave, noise, and transient pulse */
            double t = (double)i / (double)N;
            float val = (float)(0.5 * sin(2.0 * M_PI_DOUBLE * 10.5 * t) +
                                0.25 * sin(2.0 * M_PI_DOUBLE * 45.2 * t));
            /* Add some noise */
            val += (float)(0.05 * ((double)rand() / RAND_MAX - 0.5));
            /* Add a sharp transient in the middle */
            if (i == N / 2) {
                val += 10.0f;
            }
            data_ref[i] = val;
            data_opt[i] = val;
        }

        /* Run reference */
        reference_MDCT(&fft_tables, data_ref, N, work_ref);

        /* Run optimized */
        optimized_MDCT(&fft_tables, data_opt, N, work_opt);

        /* Verify SNR */
        double snr = calculate_snr(data_ref, data_opt, N);
        printf("  SNR Verification: %.6f dB\n", snr);

        /* Find Max Absolute Error */
        float max_err = 0.0f;
        int max_err_idx = -1;
        for (i = 0; i < N; i++) {
            float err = fabsf(data_ref[i] - data_opt[i]);
            if (err > max_err) {
                max_err = err;
                max_err_idx = i;
            }
        }
        printf("  Max Absolute Error: %e (at index %d)\n", max_err, max_err_idx);

        if (snr >= 130.0) {
            printf("  [PASS] Mathematical correctness is fully verified (SNR >= 130 dB).\n");
        } else {
            printf("  [FAIL] SNR delta is below 130 dB threshold!\n");
        }

        /* Benchmark execution time */
        int iterations = (N == 2 * BLOCK_LEN_LONG) ? 50000 : 400000;
        printf("  Benchmarking over %d iterations...\n", iterations);

        /* Ref benchmark */
        clock_t start_ref = clock();
        for (int iter = 0; iter < iterations; iter++) {
            /* Reload data */
            for (i = 0; i < N; i++) data_ref[i] = 1.0f; /* dummy constant data for speed */
            reference_MDCT(&fft_tables, data_ref, N, work_ref);
        }
        clock_t end_ref = clock();
        double time_ref = (double)(end_ref - start_ref) / CLOCKS_PER_SEC;

        /* Opt benchmark */
        clock_t start_opt = clock();
        for (int iter = 0; iter < iterations; iter++) {
            /* Reload data */
            for (i = 0; i < N; i++) data_opt[i] = 1.0f;
            optimized_MDCT(&fft_tables, data_opt, N, work_opt);
        }
        clock_t end_opt = clock();
        double time_opt = (double)(end_opt - start_opt) / CLOCKS_PER_SEC;

        printf("  Reference Time: %.3f seconds\n", time_ref);
        printf("  Optimized Time: %.3f seconds\n", time_opt);
        printf("  Speedup: %.2f%%\n\n", ((time_ref / time_opt) - 1.0) * 100.0);

        free(data_ref);
        free(data_opt);
        free(work_ref);
        free(work_opt);
    }

    fft_terminate(&fft_tables);
    return 0;
}
