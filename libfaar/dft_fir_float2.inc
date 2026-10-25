/*
 * FAAR - Freeware Advanced Audio Resampler
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
/* High's compensated float prefilter. Included after the ordinary kernels. */
#ifdef FAAR_COMPENSATED_FLOAT
static int init_precise_plans(faar_dft_fir *stage, const faar_dft_fir *owner) {
    size_t nin = stage->fft_size / stage->expansion;
    size_t nout = stage->fft_size / stage->decimation;
    if (nout > nin) {
        if (faar_float2_init_shared(&stage->precise_out, nout,
                    owner ? &owner->precise_out : NULL)) return -1;
        return faar_float2_init_shared(&stage->precise_in, nin, &stage->precise_out);
    }
    if (faar_float2_init_shared(&stage->precise_in, nin,
                owner ? &owner->precise_in : NULL)) return -1;
    return nout == nin ? 0
        : faar_float2_init_shared(&stage->precise_out, nout, &stage->precise_in);
}

static int init_precise_spectrum(faar_dft_fir *stage, const double *taps) {
    size_t n = stage->fft_size;
    if (init_precise_plans(stage, NULL)) return -1;
    stage->spec = AllocMemory(2 * n * sizeof(*stage->spec));
    stage->spec_low = AllocMemory(2 * n * sizeof(*stage->spec_low));
    if (!stage->spec || !stage->spec_low) return -1;
    faar_float2_plan temporary = {0};
    faar_float2_plan *plan = stage->precise_in.size == n ? &stage->precise_in
        : stage->precise_out.size == n ? &stage->precise_out : &temporary;
    if (plan == &temporary && faar_float2_init(plan, n)) return -1;
    faar_float2_prepare(plan);
    for (size_t i = 0; i < stage->tap_count; ++i) plan->work[i] = ff2_setup(taps[i]);
    faar_float2_forward(plan);
    /* 1/N is exact for the supported power-of-two transforms. Keep both
     * words: quantizing the spectrum to one float adds block residuals. */
    float scale = 1.0f / (float)n;
    for (size_t i = 0; i < 2 * n; ++i) {
        stage->spec[i] = plan->work[i].hi * scale;
        stage->spec_low[i] = plan->work[i].lo * scale;
    }
    faar_float2_clear(&temporary);
    return 0;
}

static void precise_load(faar_float2 *z, const float *overlap, size_t stored,
                         const float *input, size_t from, size_t count) {
    for (size_t i = 0; i < count; ++i)
        z[i] = ff2(from + i < stored ? overlap[from + i]
                                    : input[from + i - stored]);
}

static void convolve_precise(faar_dft_fir *stage, const float *input, float *output) {
    const size_t n = stage->fft_size, nin = n / stage->expansion;
    const size_t nout = n / stage->decimation;
    const size_t stored = (stage->tap_count - 1) / stage->expansion;
    const size_t stride = overlap_stride(stage);
    faar_float2_plan *forward = &stage->precise_in;
    faar_float2 *zr = forward->work, *zi = zr + nin;
    precise_load(zr, stage->overlap, stored, input, 0, nin);
    if (stage->stacked)
        precise_load(zi, stage->overlap, stored, input, stage->input_block_size, nin);
    else if (stage->lanes > 1)
        precise_load(zi, stage->overlap + stride, stored,
                     input + stage->stream_in, 0, nin);
    else
        memset(zi, 0, nin * sizeof(*zi));
    faar_float2_forward(forward);
    faar_float2_plan *inverse = stage->precise_out.size ? &stage->precise_out : forward;
    faar_float2 *wr = inverse->work, *wi = wr + nout;
    for (size_t k = 0; k < nout; ++k) {
        faar_float2 r = ff2(0), im = ff2(0);
        for (size_t p = 0; p < stage->decimation; ++p) {
            size_t at = k + p * nout, from = at % nin;
            faar_float2 xr = zr[from], xi = zi[from];
            faar_float2 hr = {stage->spec[at], stage->spec_low[at]};
            faar_float2 hi = {stage->spec[n + at], stage->spec_low[n + at]};
            r = ff2_add(r, ff2_sub(ff2_mul(xr, hr), ff2_mul(xi, hi)));
            im = ff2_sub(im, ff2_add(ff2_mul(xr, hi), ff2_mul(xi, hr)));
        }
        wr[k] = r; wi[k] = im;
    }
    /* Stored conjugated, so the forward transform also serves as inverse. */
    faar_float2_forward(inverse);
    size_t skip = (stage->tap_count - 1) / stage->decimation;
    for (size_t i = 0; i < stage->output_block_size; ++i)
        output[i] = ff2_round(wr[skip + i]);
    if (stage->lanes > 1 || stage->stacked)
        for (size_t i = 0; i < stage->output_block_size; ++i)
            output[stage->output_block_size + i] = -ff2_round(wi[skip + i]);
}

static void process_precise_halfband(faar_dft_fir *stage, float *output) {
    size_t n = stage->tap_count, count = stage->block_size / 2;
    faar_float2 *acc = stage->precise_half_work;
    for (size_t i = 0; i < count; ++i)
        acc[i] = ff2_mulf(stage->precise_half_coeffs[n / 4], stage->work[2 * i + n / 2]);
    for (size_t j = 0; j < n / 4; ++j) {
        faar_float2 coefficient = stage->precise_half_coeffs[j];
        const float *x = stage->work + 2 * j + 1;
        const float *y = stage->work + n - 2 - 2 * j;
        for (size_t i = 0; i < count; ++i) {
            faar_float2 pair = ff2_add(ff2(x[2 * i]), ff2(y[2 * i]));
            acc[i] = ff2_add(acc[i], ff2_mul(coefficient, pair));
        }
    }
    for (size_t i = 0; i < count; ++i) output[i] = ff2_round(acc[i]);
}
#endif
