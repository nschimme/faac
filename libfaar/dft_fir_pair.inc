/* Paired convolution in the build-selected sample precision. */
#define PAIR_JOIN_(a, b) a##b
#define PAIR_JOIN(a, b) PAIR_JOIN_(a, b)
#define PAIR_FN(name) PAIR_JOIN(name, PAIR_TAG)
#define PAIR_FFT_FN(name) PAIR_JOIN(PAIR_FFT, name)

static void PAIR_FN(spectrum_product)(PAIR_REAL * restrict wr,
                                      PAIR_REAL * restrict wi,
                                      const PAIR_REAL * restrict xr,
                                      const PAIR_REAL * restrict xi,
                                      const PAIR_REAL * restrict hr,
                                      const PAIR_REAL * restrict hi,
                                      size_t count, int add) {
    /* Stored conjugated, so the forward kernel also serves as the inverse. */
    if (add) {
        for (size_t j = 0; j < count; ++j) {
            wr[j] += xr[j] * hr[j] - xi[j] * hi[j];
            wi[j] -= xr[j] * hi[j] + xi[j] * hr[j];
        }
    } else {
        for (size_t j = 0; j < count; ++j) {
            wr[j] = xr[j] * hr[j] - xi[j] * hi[j];
            wi[j] = -(xr[j] * hi[j] + xi[j] * hr[j]);
        }
    }
}

/* Window `from` of the stream: stored overlap followed by the new input. */
static void PAIR_FN(load_window)(PAIR_REAL *z, const faar_sample_t *overlap,
                                 size_t stored, const faar_sample_t *input,
                                 size_t from, size_t count) {
    for (size_t i = 0; i < count; ++i)
        z[i] = (PAIR_REAL)(from + i < stored ? overlap[from + i]
                                             : input[from + i - stored]);
}

/* spectrum is the scaled filter response, n real parts then n imaginary. */
static int PAIR_FN(init_transforms)(faar_dft_fir *stage,
                                    const faar_sample_t *spectrum) {
    const size_t n = stage->fft_size, nout = n / stage->decimation;
    PAIR_REAL *h = (PAIR_REAL *)malloc(2 * n * sizeof(PAIR_REAL));
    if (!h) return -1;
    for (size_t i = 0; i < 2 * n; ++i) h[i] = (PAIR_REAL)spectrum[i];
    stage->PAIR_FN(spec) = h;
    if (PAIR_FFT_FN(cinit)(&stage->PAIR_FN(in), (int)(n / stage->expansion)))
        return -1;
    if (nout != n / stage->expansion
            && PAIR_FFT_FN(cinit)(&stage->PAIR_FN(out), (int)nout)) return -1;
    return 0;
}

/* The imaginary part carries a second channel, or for a lone channel the
 * following block, whose window starts one block into the stacked input.
 * Zero insertion tiles the input spectrum and decimation sums its aliases;
 * both are linear, so the two parts stay independent. */
static void PAIR_FN(convolve_paired)(faar_dft_fir *stage, const faar_sample_t *input,
                                     faar_sample_t *output) {
    const size_t n = stage->fft_size, lanes = stage->lanes;
    const size_t decimation = stage->decimation;
    const size_t nin = n / stage->expansion, nout = n / decimation;
    const size_t stored = (stage->tap_count - 1) / stage->expansion;
    const size_t stride = overlap_stride(stage);
    const size_t unit = nin < nout ? nin : nout;
    const PAIR_PLAN *inverse = stage->PAIR_FN(out).size
        ? &stage->PAIR_FN(out) : &stage->PAIR_FN(in);
    const PAIR_REAL *hr = stage->PAIR_FN(spec), *hi = hr + n;
    PAIR_REAL *zr = PAIR_FFT_FN(cbuffer)(&stage->PAIR_FN(in), 0), *zi = zr + nin;
    PAIR_FN(load_window)(zr, stage->overlap, stored, input, 0, nin);
    if (stage->stacked)
        PAIR_FN(load_window)(zi, stage->overlap, stored, input,
                             stage->input_block_size, nin);
    else if (lanes > 1)
        PAIR_FN(load_window)(zi, stage->overlap + stride, stored,
                             input + stage->stream_in, 0, nin);
    else
        memset(zi, 0, nin * sizeof(PAIR_REAL));
    const PAIR_REAL *xr = PAIR_FFT_FN(cforward)(&stage->PAIR_FN(in), zr),
                    *xi = xr + nin;
    PAIR_REAL *wr = PAIR_FFT_FN(cbuffer)(inverse, 0);
    if (wr == xr) wr = PAIR_FFT_FN(cbuffer)(inverse, 1);
    PAIR_REAL *wi = wr + nout;
    for (size_t k = 0; k < nout; k += unit)
        for (size_t p = 0; p < decimation; ++p) {
            const size_t at = k + p * nout, from = at % nin;
            PAIR_FN(spectrum_product)(wr + k, wi + k, xr + from, xi + from,
                                      hr + at, hi + at, unit, p != 0);
        }
    const PAIR_REAL *yr = PAIR_FFT_FN(cforward)(inverse, wr), *yi = yr + nout;
    const size_t skip = (stage->tap_count - 1) / decimation;
    const size_t count = stage->output_block_size;
    for (size_t i = 0; i < count; ++i) output[i] = yr[skip + i];
    if (lanes > 1 || stage->stacked)
        for (size_t i = 0; i < count; ++i) output[count + i] = -yi[skip + i];
}

#undef PAIR_JOIN_
#undef PAIR_JOIN
#undef PAIR_FN
#undef PAIR_FFT_FN
