# libfaar - Polyphase Farrow-Interpolated Kaiser-Sinc Audio Resampler Library

`libfaar` is a standalone, lightweight, high-precision audio sample rate conversion C library included with FAAC. It uses a polyphase filter bank with Kaiser windowing and Farrow cubic polynomial phase interpolation to deliver continuous, infinite-resolution fractional delays with >140 dB stopband attenuation and <0.001 dB passband ripple.

## Key Features

- **Continuous Fractional Delay**: Farrow cubic polynomial phase interpolation between 256 pre-calculated polyphase filter slices.
- **High Audio Precision**: Filter coefficients designed in 64-bit double precision (`f64`), processing in 32-bit single precision (`float`).
- **SIMD Optimized**: Straight-line "Interpolate-Then-Convolve" dot-product loops designed for GCC, Clang, and MSVC auto-vectorization (AVX2, AVX-512, NEON).
- **Multi-Channel & Surround Support**: Multi-channel interleaved audio streams (1 to 8 channels, including 5.1 and 7.1 surround sound).
- **Multi-Stage Decimation**: Automatic cascade decomposition for downsampling ratios >2:1 (e.g. 192 kHz -> 24 kHz).

## API Overview

Include header: `#include <faar.h>`

### Data Types & Enums

- `faar_handle_t`: Opaque handle to a resampler instance.
- `faar_quality_t`:
  - `FAAR_QUALITY_FAST`: 127-tap filter, 28 dB Kaiser beta (~100 dB stopband rejection)
  - `FAAR_QUALITY_GOOD`: 255-tap filter, 32 dB Kaiser beta (~120 dB stopband rejection)
  - `FAAR_QUALITY_HIGH`: 511-tap filter, 38 dB Kaiser beta (>140 dB stopband rejection)

### Functions

```c
/* Create a resampler instance */
faar_handle_t faar_create(uint32_t in_rate, uint32_t out_rate, uint16_t channels, faar_quality_t quality);

/* Query filter group delay in input frames */
size_t faar_get_delay(faar_handle_t handle);

/* Process interleaved float PCM audio samples */
size_t faar_process(faar_handle_t handle, const float *in_samples, size_t in_count,
                    float *out_samples, size_t max_out_count);

/* Flush pipeline history at stream end */
size_t faar_flush(faar_handle_t handle, float *out_samples, size_t max_out_count);

/* Destroy resampler instance and release memory */
void faar_destroy(faar_handle_t handle);
```

## Example Usage

```c
#include <stdio.h>
#include <stdlib.h>
#include <faar.h>

int main(void) {
    uint32_t in_rate = 96000;
    uint32_t out_rate = 44100;
    uint16_t channels = 2;

    faar_handle_t r = faar_create(in_rate, out_rate, channels, FAAR_QUALITY_HIGH);
    if (!r) return 1;

    size_t in_frames = 1024;
    float in_buf[1024 * 2] = {0};
    float out_buf[2048 * 2];

    size_t out_produced = faar_process(r, in_buf, in_frames, out_buf, 2048);
    size_t out_flushed = faar_flush(r, out_buf + out_produced * channels, 2048 - out_produced);

    faar_destroy(r);
    return 0;
}
```
