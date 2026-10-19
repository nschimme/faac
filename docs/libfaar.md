# libfaar - CPU Audio Resampler Library

`libfaar` (Freeware Advanced Audio Resampler) is a C library for digital sample rate conversion. Fresh Meson builds select the staged-short CPU backend, which combines an overlap-save DFT prefilter with a rate-planned fractional-delay stage. The legacy polyphase backend remains available for comparison. Input and output use float PCM; the selected backend uses double-precision filter and FFT arithmetic. Quality, transition bandwidth, CPU cost, and memory use depend on the preset and conversion ratio.

Choose `-Dfaar-backend=polyphase` to build the legacy comparison backend, or `-Dfaar-backend=staged` to build the earlier staged implementation. `staged-short` is the default.

---

## Implementation

- **Three filter presets**
  Fast, Good, and High select rate-dependent filter plans. The legacy polyphase backend uses 127, 255, and 511 taps respectively; staged-short designs its DFT prefilter and phase bank for the conversion rate.

- **Rate-planned filtering**
  Staged-short uses an overlap-save DFT anti-alias filter and a short fractional-delay bank where the plan calls for one. Integer phase arithmetic preserves the requested ratio across streaming block boundaries.

### Common conversion plans

Fresh Meson builds use `staged-short`. The routes below describe which plan that
backend selects for common AAC conversion rates; they do not imply that every
route has the same measured quality or performance. The verification tools and
their current coverage are described in
[`libfaar/verification/README.md`](../libfaar/verification/README.md).

| Conversion | `staged-short` plan |
|---|---|
| 44.1 → 48 kHz | Dedicated upsampling plan: 2× DFT expansion, then a 21-phase fractional-delay bank |
| 48 → 44.1 kHz | Dedicated reference plan: 2× DFT expansion, then a 20-tap bank; Fast/Good use spec-designed prefilters |
| 44.1 → 22.05 or 48 → 24 kHz | Dedicated half-rate plan: 2× DFT decimation and a 1-phase bank |
| 88.2 → 44.1 kHz | Dedicated half-rate plan: 2× DFT decimation and a 1-phase bank |
| 88.2 → 48 kHz | General staged downsampling plan: DFT prefilter and a 127-phase bank |
| 96 → 44.1 kHz | Dedicated reference plan: DFT prefilter and a 20-tap bank; Fast/Good use spec-designed prefilters |
| 96 → 48 kHz | Dedicated half-rate plan: 2× DFT decimation and a 1-phase bank |
| 44.1 → 88.2, 48 → 96, or 96 → 192 kHz | Dedicated exact-double upsampling plan: 2× DFT expansion and a 1-phase bank |
| 44.1 → 96 or 48 → 88.2 kHz | Dedicated fractional upsampling plan: 2× DFT expansion and a 21-phase bank |
| 32 → 44.1 or 48 kHz; 88.2 → 96 kHz | Dedicated upsampling plan: 2× DFT expansion and a 21-phase bank |
| 48 → 32 kHz | Spec-designed 2× DFT expansion, then exact 3:1 sampling with a 1-tap bank; High uses a sharper stopband for near-Nyquist alias rejection |
| 44.1 → 32 kHz | Spec-designed 2× DFT expansion, then a 21-tap fractional-delay bank |
| 176.4 → 88.2 kHz | Dedicated half-rate plan: 2× DFT decimation and a 1-phase bank |
| 176.4 → 96, 48, or 44.1 kHz | General staged downsampling plan: DFT prefilter and a 127-phase bank |
| 192 → 96 kHz | Dedicated half-rate plan: 2× DFT decimation and a 1-phase bank |
| 192 → 48 kHz | Dedicated 4:1 plan: 4× DFT decimation and a 1-phase bank |
| 192 → 44.1 kHz | Dedicated reference plan: DFT prefilter and a 20-tap bank; Fast/Good use spec-designed prefilters |

The fractional-delay bank length is a plan parameter, not the DFT prefilter's
tap count. Filter tap counts and coefficients vary by quality preset. Other
downsampling routes with output/input ratios from 0.20 through 0.95 use the
general staged plan; conversions outside the staged routes use the legacy
polyphase path. Setting `-Dfaar-backend=polyphase` selects that legacy backend
for all conversions, while `-Dfaar-backend=staged` selects the earlier staged
planner.

- **Aligned streaming output**
  The first output corresponds to input time zero, and flushing produces `ceil(input_frames * out_rate / in_rate)` output frames. The default boundary mode holds the first and last sample outside a finite stream; `FAAR_BOUNDARY_ZERO` selects silence instead. Filter lookahead is available through `faar_get_delay()` in input frames, including cascaded stages.

- **Quantization and noise shaping**
  When downconverting floating-point audio to 16-bit or 24-bit integer PCM formats, `libfaar` applies Triangular Probability Density Function (TPDF) dithering and optional high-pass noise-shaping error feedback (`FAAR_DITHER_SHAPED`), shifting dither noise energy into psychoacoustically less sensitive treble frequencies.

- **Portable processing loops**
  Filter coefficients and history are contiguous, with 64-byte alignment. The implementation uses portable C and includes its FFT implementation; it does not require an external DSP library.

- **Multi-Channel & Surround Support**
  Native support for 1 to 8 channels (including 5.1 and 7.1 surround sound) with unit-stride de-interleaving and automatic multi-stage decimation cascades for extreme downsampling ratios ($>2:1$).

---

## API Documentation

Include header: `#include <faar.h>`

### Data Types & Enums

#### `faar_handle_t`
Opaque handle to a resampler instance.

#### `faar_quality_t`
- `FAAR_QUALITY_FAST`: lowest-cost plan
- `FAAR_QUALITY_GOOD`: balanced plan
- `FAAR_QUALITY_HIGH`: highest-quality plan

#### `faar_dither_t`
- `FAAR_DITHER_OFF`: Float output without quantization or dithering
- `FAAR_DITHER_TPDF`: Triangular Probability Density Function dither
- `FAAR_DITHER_SHAPED`: TPDF dither with high-pass noise shaping error feedback

#### `faar_boundary_t`
- `FAAR_BOUNDARY_HOLD`: Continue the first and last sample beyond the supplied stream (default)
- `FAAR_BOUNDARY_ZERO`: Treat samples outside the supplied stream as silence

---

### Functions

```c
/* Create a new resampler instance */
FAARAPI faar_handle_t faar_create(uint32_t in_rate, uint32_t out_rate, uint16_t channels, faar_quality_t quality);

/* Configure dithering and noise shaping for integer PCM outputs */
FAARAPI int faar_set_dither(faar_handle_t handle, faar_dither_t dither, uint8_t target_bits);

/* Select endpoint extension before processing or flushing */
FAARAPI int faar_set_boundary(faar_handle_t handle, faar_boundary_t boundary);

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
  --boundary <mode>      Endpoint extension (hold, zero) [default: hold]
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
