/*
 * ARDFTSRC - Arbitrary Rate Discrete Fourier Transform Sample Rate Converter
 * C Translation for FAAC
 */

#ifndef ARDFTSRC_H
#define ARDFTSRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArdftsrcResampler ArdftsrcResampler;

/* Create a new ARDFTSRC resampler instance.
 * in_rate: source sampling rate (Hz)
 * out_rate: target sampling rate (Hz)
 * channels: number of interleaved audio channels
 * quality: 0 = fast (127-tap, beta=24), 1 = good (255-tap, beta=30), 2 = high (511-tap, beta=34)
 * Returns NULL on allocation failure or invalid parameters.
 */
ArdftsrcResampler *ardftsrc_create(uint32_t in_rate, uint32_t out_rate, uint16_t channels, int quality);

/* Process a chunk of interleaved float PCM samples.
 * in_samples: pointer to input float array (interleaved)
 * in_count: number of input frames (samples per channel)
 * out_samples: pointer to output buffer (allocated by caller)
 * max_out_count: capacity of out_samples in frames (samples per channel)
 * Returns the actual number of output frames written to out_samples.
 */
size_t ardftsrc_process(ArdftsrcResampler * restrict r, const float * restrict in_samples, size_t in_count,
                        float * restrict out_samples, size_t max_out_count);

/* Flush delayed tail frames from internal overlap buffers.
 * out_samples: pointer to output buffer
 * max_out_count: capacity of out_samples in frames
 * Returns number of tail frames written.
 */
size_t ardftsrc_flush(ArdftsrcResampler *r, float *out_samples, size_t max_out_count);

/* Free an ARDFTSRC resampler instance. */
void ardftsrc_destroy(ArdftsrcResampler *r);

/* Calculate expected delay in frames. */
size_t ardftsrc_get_delay(const ArdftsrcResampler *r);

#ifdef __cplusplus
}
#endif

#endif /* ARDFTSRC_H */
