# libfaar - CPU Audio Resampler Library

`libfaar` (Freeware Advanced Audio Resampler) is a C library for digital sample rate conversion. The staged-short CPU backend combines an overlap-save DFT prefilter with a rate-planned fractional-delay stage. Input and output use float PCM; staged-short uses double-precision filter and FFT arithmetic. Quality, transition bandwidth, CPU cost, and memory use depend on the preset and conversion ratio.

Staged-short is the only build. Its polyphase bank handles routes without a dedicated plan and the final stage after the DFT prefilter.

---

## Implementation

- **Three filter presets**
  Fast, Good, and High select rate-dependent filter plans. The fallback polyphase bank uses 127, 255, and 511 taps respectively; staged-short designs its DFT prefilter and phase bank for the conversion rate.

- **Rate-planned filtering**
  Staged-short uses an overlap-save DFT anti-alias filter and a short fractional-delay bank where the plan calls for one. Integer phase arithmetic preserves the requested ratio across streaming block boundaries.

### Common conversion plans

Fresh Meson builds use `staged-short`. Plans combine a DFT prefilter with an
exact sampling stage or fractional-delay bank. The verification tools and their
coverage are described in
[`libfaar/verification/README.md`](../libfaar/verification/README.md).

| Conversion class | `staged-short` plan |
|---|---|
| General downsampling | Evaluate zero or more leading wide-transition 2:1 half-band stages using the calibrated work model below, then choose the largest DFT decimation D in {4, 2, 1}, then the smallest expansion E in {1, 2}, such that the bank rate R = input × E / D is at least 2.50 × output. Keep the final bank rate and tap count no greater than the original general plan and require the same 2.50 margin after each halving. Design the sharp prefilter at the reduced input rate against FINAL output Nyquist. Use exact 1-tap sampling when R/output is integral; otherwise use a 21-tap Kaiser bank. |
| Near-unity downsampling where 2× expansion cannot reach that margin | Use 2× expansion and the wider-bandwidth 20-tap prototype bank. |
| Dedicated half-rate and 192 → 48 kHz plans | Keep 2× and 4× DFT decimation respectively, followed by exact 1-tap sampling. Fast/Good use spec-designed filters; High retains the measured filter. |
| Dedicated reference plans to 44.1 kHz | Keep the 20-tap prototype bank and existing expansion. Fast/Good use spec-designed prefilters. High retains the measured 48/96 kHz designs; 192 kHz uses a spec-designed filter whose stop edge also covers the near-Nyquist alias probe. |
| Dedicated plans to 32 kHz | Keep 2× expansion, with exact 3:1 sampling for 48 kHz input and a 21-tap bank for 44.1 kHz input. High's 48 kHz filter uses the sharper existing stop edge. |
| Dedicated 2× expansion upsampling | Design Fast/Good against input Nyquist. Keep measured High filters except the corrected 32 kHz input plans. Exact double-rate routes use a 1-tap bank; the measured prototype is retained for 44.1 → 48 kHz, and generic fractional banks use 41 taps to suppress phase modulation. Measured High banks remain unchanged except for 32 kHz input. |

The planner evaluates every eligible `k`, including `k=0`, and minimizes
work per final output sample. For each candidate, `F` is the sum of paired
half-band multiply-adds, `(stage_input / (2 * final_output)) * (taps/4 + 1)`;
`T` is the final FFT work divided by its usable block length and scaled to
final outputs; `B` is the bank length. Forward and inverse FFT sizes are
`N/E` and `N/D`, reflecting zero-insertion and spectral folding, so
`T = ((N/E)*log2(N/E) + (N/D)*log2(N/D)) / block_final_outputs`,
where `block_final_outputs = (N-taps+1)*final_output/(stage_input*E)`.
`V` counts input frames visited by all stages per final output. The fitted
cost is `0.41445*F + 0.29928*T + 0.21198*B + 0.74921*V`. Constants were fitted
once to 93 candidate/preset cases measured in three alternating rounds of
30 s stereo, with nonnegative least squares and constraints rejecting
calibration candidates measured more than 3% slower than `k=0`. There is no
per-route cost table. The fitted constant output overhead was zero. `k=0` retains the original
filter and 5.3 dB margin; only cascaded candidates add the 4.7 dB reserve.

Leading stages apply only to GENERAL downsampling; dedicated HALF, QUARTER,
REFERENCE, DOWN32 and UP plans retain their existing designs. Each leading
stage stops aliases before they can land inside 1.04 times the final output
Nyquist. Its required pass edge is the preset's 0.90/0.96 times that Nyquist;
we conservatively extend it to 1.04, paired with the stop edge
`stage_input/2 - 1.04 * final_output/2`. These symmetric edges centre the
filter at one quarter of the stage input rate, making alternate non-centre
taps zero. Lengths are rounded to `4m+1`. A sparse time-domain FIR pairs the
symmetric odd taps and vectorizes across independent outputs. It uses the
same centred prefix/suffix padding and HOLD/ZERO endpoints as the DFT FIR.
The cascade retains the original duration cap, composes delay in input frames,
and leaves dither to the final stage.

Cascade filters share the preset Kaiser design and 5.3 dB margin, with an
additional 4.7 dB reserve for shorter final-filter sidelobes and the added float
boundary. The final stage keeps the preset pass/stop edges relative to FINAL
output Nyquist. Dedicated designs do not use this additional reserve.

The 21-tap bank's oversampling margin is the centre of the measured 2.48–2.52
bracket: at 0.96 Nyquist, isolated C bank probes measured residuals of −141.35,
−145.50 and −150.66 dB at ratios 2.48, 2.50 and 2.52 respectively.

Spec-designed filters use the lower Nyquist frequency: Fast preserves 90% of
that bandwidth with 100 dB rejection; Good preserves 96% with 130 dB rejection;
High preserves 96% with 150 dB rejection. The Kaiser estimate includes one
uniform 5.3 dB design margin (5.2 dB failed the full screen; 5.3 dB passed). FIR lengths
respect DFT expansion, decimation and time-zero alignment. Bank tap counts are
independent of prefilter tap counts. Preset masks define acceptance; rejection
beyond the mask and Fast response above 0.90 Nyquist can differ from earlier
plans.

Other upsampling routes use the polyphase bank directly.

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
