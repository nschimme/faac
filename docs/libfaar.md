# libfaar - Polyphase Farrow-Interpolated Kaiser-Sinc Audio Resampler Library

`libfaar` (Freeware Advanced Audio Resampler) is a lightweight, dependency-free, high-precision C library for digital sample rate conversion. Designed to combine mastering-grade signal fidelity with high execution throughput, `libfaar` operates across arbitrary sampling ratios without phase distortion, aliasing artifacts, or quantization noise.

---

## Technical Highlights & Audiophile Benefits

- **Mastering-Grade Aliasing Suppression (>140 dB Stopband Rejection)**
  High-quality mode uses a 511-tap Kaiser window filter ($\beta = 38.0$) to push aliasing products completely below the noise floor of digital audio (-140 dB), surpassing commercial mastering standards.

- **Sample-Accurate Phase Continuity (Farrow Cubic Phase Interpolation)**
  Rather than truncating or rounding to nearest discrete polyphase steps, `libfaar` evaluates Farrow cubic polynomials continuously across 256 pre-computed polyphase filter slices. This provides infinite fractional delay resolution and eliminates phase quantization noise.

- **Zero Phase Distortion (Linear Phase Symmetry)**
  Filter banks are constructed with perfect linear phase symmetry, ensuring uniform group delay across all frequencies and preserving acoustic transients without phase smearing.

- **Audiophile Quantization & Noise Shaping**
  When downconverting floating-point audio to 16-bit or 24-bit integer PCM formats, `libfaar` applies Triangular Probability Density Function (TPDF) dithering and optional high-pass noise-shaping error feedback (`FAAR_DITHER_SHAPED`), shifting dither noise energy into psychoacoustically less sensitive treble frequencies.

- **High Performance & Auto-Vectorized Architecture**
  Filter dot products are implemented in flat, unit-stride loops with 64-byte memory alignment (`alignas(64)`), enabling SIMD auto-vectorization across AVX2, AVX-512, and ARM NEON ($180\text{x}-800\text{x}$ realtime).

- **Multi-Channel & Surround Support**
  Native support for 1 to 8 channels (including 5.1 and 7.1 surround sound) with unit-stride de-interleaving and automatic multi-stage decimation cascades for extreme downsampling ratios ($>2:1$).

---

## API Documentation

Include header: `#include <faar.h>`

### Data Types & Enums

#### `faar_handle_t`
Opaque handle to a resampler instance.

#### `faar_quality_t`
- `FAAR_QUALITY_FAST`: 127-tap filter, 28 dB Kaiser beta (~100 dB stopband rejection)
- `FAAR_QUALITY_GOOD`: 255-tap filter, 32 dB Kaiser beta (~120 dB stopband rejection)
- `FAAR_QUALITY_HIGH`: 511-tap filter, 38 dB Kaiser beta (>140 dB stopband rejection)

#### `faar_dither_t`
- `FAAR_DITHER_OFF`: Direct rounding without dithering
- `FAAR_DITHER_TPDF`: Triangular Probability Density Function dither
- `FAAR_DITHER_SHAPED`: TPDF dither with high-pass noise shaping error feedback

---

### Functions

```c
/* Create a new resampler instance */
FAARAPI faar_handle_t faar_create(uint32_t in_rate, uint32_t out_rate, uint16_t channels, faar_quality_t quality);

/* Configure dithering and noise shaping for integer PCM outputs */
FAARAPI int faar_set_dither(faar_handle_t handle, faar_dither_t dither, uint8_t target_bits);

/* Query filter group delay in input samples per channel */
FAARAPI size_t faar_get_delay(faar_handle_t handle);

/* Process interleaved float PCM audio samples */
FAARAPI size_t faar_process(faar_handle_t handle, const float *in_samples, size_t in_count,
                            float *out_samples, size_t max_out_count);

/* Flush pipeline history at end of stream */
FAARAPI size_t faar_flush(faar_handle_t handle, float *out_samples, size_t max_out_count);

/* Destroy resampler instance and free memory */
FAARAPI void faar_destroy(faar_handle_t handle);
```

---

## FAAR CLI Usage

`faar` is the companion command-line executable:

```
FAAR - Freeware Advanced Audio Resampler
Usage: faar [options] <input> <output>

Core Options:
  -r, --rate <Hz>        Target sample rate (Required, e.g., 48000, 44100)
  --quality <preset>     DSP filter quality (fast, good, high) [default: good]
  -c, --channels <num>   Audio channel count [default: 2]
  -f, --format <fmt>     Raw PCM sample format (f32le, s16le, s24le) [default: f32le]
  -d, --dither <type>    Dithering type for integer format output (none, tpdf, shaped) [default: none]
  --decimate             Force multi-stage decimation cascade for large downsampling ratios
  --ignorelength         Ignore the WAV data length in the header and read to EOF
  -v <level>             Set verbosity level (-v0 is quiet mode) [default: 1]
  -o, --output <path>    Explicit output path (or use - for stdout)
  -h, --help             Display usage guide
```

### Examples

```bash
# Resample 96 kHz WAV master to 48 kHz WAV
faar -r 48000 input_96k.wav output_48k.wav

# Resample to 16-bit WAV using High Quality, TPDF dithering, and quiet mode (-v0)
faar -r 48000 --quality high -d tpdf -f s16le -v0 input.wav output.wav

# Stream raw 24-bit PCM through stdin/stdout pipeline
cat input.raw | faar -r 48000 -c 2 -f s24le -d shaped - - | aplay -f S24_LE -r 48000
```
