#ifndef FAAR_DFT_FIR_H
#define FAAR_DFT_FIR_H

#include "fft_double.h"

#include <stddef.h>

/* Block overlap-save FIR stage; filter coefficients are supplied by caller. */
typedef struct faar_dft_fir {
    faar_real_fft_plan fft;
    faar_real_fft_plan input_fft;
    faar_real_fft_plan decimated_fft;
    size_t fft_size;
    size_t tap_count;
    size_t expansion;
    size_t block_size;
    size_t output_block_size;
    size_t input_block_size;
    size_t decimation;
    double *filter_spectrum;
    double *work;
    double *overlap;
    double *input_block;
    double *input_spectrum;
    double *output_block;
    size_t input_fill;
    size_t output_count;
    size_t output_offset;
    int finishing;
    int finished;
} faar_dft_fir;

/* fft_size must be at least tap_count and have only 2, 3, 5, 7 prime factors. */
int faar_dft_fir_init(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size);
int faar_dft_fir_init_expanded(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion);
int faar_dft_fir_init_rate(faar_dft_fir *stage, const double *taps,
                      size_t tap_count, size_t fft_size, size_t expansion,
                      size_t decimation);
void faar_dft_fir_clear(faar_dft_fir *stage);

/* Consumes and produces exactly stage->block_size mono samples per call. */
int faar_dft_fir_process(faar_dft_fir *stage, const double *input,
                         double *output);

/* Stream arbitrary chunks. consumed/produced are always set. Output capacity
 * may be smaller than a transform block; remaining output is retained. */
int faar_dft_fir_stream(faar_dft_fir *stage, const double *input,
                        size_t input_count, size_t *consumed,
                        double *output, size_t output_capacity,
                        size_t *produced);

/* Finish a stream, emitting the remaining real input samples. Call until 0. */
int faar_dft_fir_flush(faar_dft_fir *stage, double *output,
                       size_t output_capacity, size_t *produced);

/* Return to the zero-history initial state. */
void faar_dft_fir_reset(faar_dft_fir *stage);

#endif
