#ifndef FAAR_DFT_FIR_H
#define FAAR_DFT_FIR_H

#include "fft_real.h"
#include "libfaar_internal.h"

#include <stddef.h>

/* Block overlap-save FIR stage; filter coefficients are supplied by caller. */
typedef struct faar_dft_fir {
    int halfband; /* Sparse time-domain 2:1 FIR, sharing stream storage. */
    size_t fft_size;
    size_t tap_count;
    size_t expansion;
    size_t block_size;
    size_t output_block_size;
    size_t input_block_size;
    size_t decimation;
    faar_sample_t *filter_spectrum;
    faar_sample_t *work;
    faar_sample_t *overlap;
    faar_sample_t *input_block;
    faar_sample_t *output_block;
    int paired;
    int stacked;
    size_t stream_in; /* samples per lane the stage takes before it runs */
    size_t stream_out;
    size_t lanes;
    faar_fft_plan in, out;
    faar_sample_t *spec;
    size_t input_fill;
    size_t output_count;
    size_t output_offset;
    int finishing;
    int finished;
} faar_dft_fir;

/* fft_size must be a supported power of two and at least tap_count.
 * init_rate also accepts zero for the sparse half-band 2:1 stage. */
int faar_dft_fir_init(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size);
int faar_dft_fir_init_expanded(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion);
int faar_dft_fir_init_rate(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation);
/* Paired stage: `lanes` (1 or 2) channels share one complex FFT as
 * z = a + jb, so the two real outputs come out as real and imaginary parts.
 * With stacked, a lone channel instead pairs each block with the next, so the
 * stage runs once per two blocks (stream_in input samples). Rates and block
 * geometry are as for init_rate; fft_size must be a power of two the complex
 * FFT supports. Lane planes are laid out one after another: input lane l
 * starts at input + l * input_count, output lane l at output + l *
 * output_capacity (process: stream_in and stream_out). */
int faar_dft_fir_init_paired(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation, size_t lanes, int stacked);
void faar_dft_fir_clear(faar_dft_fir *stage);

/* Consumes and produces exactly stage->block_size mono samples per call. */
int faar_dft_fir_process(faar_dft_fir *stage, const faar_sample_t *input,
                         faar_sample_t *output);

/* Stream arbitrary chunks. consumed/produced are always set. Output capacity
 * may be smaller than a transform block; remaining output is retained. */
int faar_dft_fir_stream(faar_dft_fir *stage, const faar_sample_t *input,
                        size_t input_count, size_t *consumed,
                        faar_sample_t *output, size_t output_capacity,
                        size_t *produced);

/* Finish a stream, emitting the remaining real input samples. Call until 0. */
int faar_dft_fir_flush(faar_dft_fir *stage, faar_sample_t *output,
                       size_t output_capacity, size_t *produced);

/* Return to the zero-history initial state. */
void faar_dft_fir_reset(faar_dft_fir *stage);

#endif
