/*
 * 2x interleaved-PCM upsampler for HE-AAC v1 on narrow-band inputs.
 *
 * faac's HE-AAC path halves the configured sample rate for the LC core
 * (libfaac/frame.c switches sampleRate /= 2 when HE is selected). On
 * a narrow-band input (16 kHz mono speech) that drops the core to
 * 8 kHz; SBR then regenerates the [4, 8] kHz band, which on speech
 * carries fricatives/sibilants -> noise replaces signal -> ~2.1 MOS.
 *
 * The fix is to upsample input 2x in the frontend before opening the
 * encoder, and pass the doubled SR to faacEncOpen. libfaac then halves
 * back to the original input SR for the LC core, and SBR generates
 * above the original Nyquist (mostly empty for bandlimited speech).
 *
 * Filter: 31-tap linear-phase symmetric halfband FIR, windowed-sinc
 * design with Hamming window. Stopband ~60 dB. Halfband property
 * (every other coefficient zero away from center) means kept-input
 * positions in the output pass through unchanged (delayed by 15 output
 * samples = 7 input samples) and only the interpolated positions go
 * through a 16-tap convolution. ~16 mults per input sample per channel.
 *
 * Self-contained: no external resampler dependency. Coefficient table
 * is original work derived from the standard windowed-sinc construction
 * (Crochiere & Rabiner, Multirate Digital Signal Processing, 1983).
 */
#ifndef FAAC_FRONTEND_UPSAMPLE_H
#define FAAC_FRONTEND_UPSAMPLE_H

typedef struct upsample2x upsample2x_t;

/* Create a 2x upsampler for `channels` interleaved float channels. */
upsample2x_t *upsample2x_create(int channels);

/* Free state. */
void upsample2x_destroy(upsample2x_t *u);

/* Process `in_samples` interleaved frames (in[in_samples * channels])
 * into out[2 * in_samples * channels]. State is preserved across calls.
 *
 * The output is delayed by 7 input frames (= 15 output frames) due to
 * filter latency. The first 15 output frames after creation are
 * transient (filter ramp-up from zero history). For a typical 10-second
 * speech clip at 16 kHz mono (~160k input samples), that's a 0.0094%
 * silence prefix.
 */
void upsample2x_process(upsample2x_t *u,
                        const float *in, int in_samples,
                        float *out);

#endif /* FAAC_FRONTEND_UPSAMPLE_H */
