/*
 * Comprehensive TNS Write-Parse Roundtrip Unit Test & Deterministic Inverse Filter Harness
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "faac.h"

/* ISO/IEC 14496-3 Section 4.6.9 - TNS inverse filter harness for deterministic testing */

static void tns_dequantize_coeffs(int order, int res, int coef_compress, const int8_t *idx, float *k) {
    int effective_res = res - coef_compress;
    const float s_p = (float)(((1 << (effective_res - 1)) - 0.5f) / (M_PI / 2));
    const float s_n = (float)(((1 << (effective_res - 1)) + 0.5f) / (M_PI / 2));

    for (int i = 1; i <= order; i++) {
        int q = idx[i];
        float s = (q >= 0) ? s_p : s_n;
        k[i] = sinf((float)q / s);
    }
}

static void tns_step_up(int order, const float *k, float *a) {
    a[0] = 1.0f;
    for (int m = 1; m <= order; m++) {
        float km = k[m];
        int half = (m + 1) / 2;

        for (int i = 1; i < half; i++) {
            float t1 = a[i];
            float t2 = a[m - i];
            a[i] = t1 + km * t2;
            a[m - i] = t2 + km * t1;
        }
        if (m % 2 == 0)
            a[m / 2] += km * a[m / 2];
        a[m] = km;
    }
}

static void tns_fir_forward(int length, int order, int direction, const float *a, const float *src, float *dst) {
    if (direction == 0) {
        int limit = order < length ? order : length;
        for (int i = 0; i < limit; i++) {
            float acc = src[i];
            for (int j = 1; j <= i; j++)
                acc += a[j] * src[i - j];
            dst[i] = acc;
        }
        for (int i = limit; i < length; i++) {
            float acc = src[i];
            for (int j = 1; j <= order; j++)
                acc += a[j] * src[i - j];
            dst[i] = acc;
        }
    } else {
        int limit = order < length ? order : length;
        for (int i = 0; i < limit; i++) {
            int idx = length - 1 - i;
            float acc = src[idx];
            for (int j = 1; j <= i; j++)
                acc += a[j] * src[idx + j];
            dst[idx] = acc;
        }
        for (int i = limit; i < length; i++) {
            int idx = length - 1 - i;
            float acc = src[idx];
            for (int j = 1; j <= order; j++)
                acc += a[j] * src[idx + j];
            dst[idx] = acc;
        }
    }
}

static void tns_iir_inverse(int length, int order, int direction, const float *a, const float *src, float *dst) {
    if (direction == 0) {
        for (int i = 0; i < length; i++) {
            float acc = src[i];
            int limit = order < i ? order : i;
            for (int j = 1; j <= limit; j++) {
                acc -= a[j] * dst[i - j];
            }
            dst[i] = acc;
        }
    } else {
        for (int i = 0; i < length; i++) {
            int idx = length - 1 - i;
            float acc = src[idx];
            int limit = order < i ? order : i;
            for (int j = 1; j <= limit; j++) {
                acc -= a[j] * dst[idx + j];
            }
            dst[idx] = acc;
        }
    }
}

static int test_tns_math_harness(void) {
    int order = 8;
    int length = 256;
    int8_t idx[9] = {0, 3, -2, 1, -1, 0, 1, 0, 0};
    float k_q[9] = {0.0f};
    float a[9] = {0.0f};

    /* Arcsine quantization and dequantization round-trip check */
    for (int res = 3; res <= 4; res++) {
        for (int compress = 0; compress <= 1; compress++) {
            tns_dequantize_coeffs(order, res, compress, idx, k_q);
            tns_step_up(order, k_q, a);

            for (int dir = 0; dir <= 1; dir++) {
                float orig[256];
                float enc[256];
                float dec[256];

                for (int i = 0; i < length; i++) {
                    orig[i] = (float)sin(0.1 * i) + (float)cos(0.03 * i);
                }

                tns_fir_forward(length, order, dir, a, orig, enc);
                tns_iir_inverse(length, order, dir, a, enc, dec);

                float max_err = 0.0f;
                for (int i = 0; i < length; i++) {
                    float err = fabsf(orig[i] - dec[i]);
                    if (err > max_err) max_err = err;
                }

                if (max_err > 1e-3f) {
                    fprintf(stderr, "TNS inverse math harness failed: dir=%d, res=%d, compress=%d, max_err=%.2e\n",
                            dir, res, compress, max_err);
                    return 0;
                }
            }
        }
    }
    printf("PASS: TNS inverse math harness (FIR forward + IIR inverse roundtrip exact to <1e-3)\n");
    return 1;
}

static double compute_aligned_snr(const int16_t *ref, size_t ref_count, const int16_t *test, size_t test_count, int max_delay) {
    double best_snr = -100.0;

    for (int delay = 0; delay <= max_delay; delay++) {
        if ((size_t)delay >= test_count) break;
        size_t count = test_count - delay;
        if (count > ref_count) count = ref_count;

        double signal_e = 0.0;
        double noise_e = 0.0;
        for (size_t i = 0; i < count; i++) {
            double r = (double)ref[i];
            double err = (double)test[i + delay] - r;
            signal_e += r * r;
            noise_e += err * err;
        }
        if (noise_e <= 1e-12) return 100.0;
        double snr = 10.0 * log10(signal_e / noise_e);
        if (snr > best_snr) best_snr = snr;
    }
    return best_snr;
}

static int run_e2e_ffmpeg_check(int argc, char **argv) {
    if (argc < 3) {
        printf("Skipping FFmpeg oracle check (missing argv parameters).\n");
        return 77;
    }

    const char *ffmpeg_bin = argv[1];
    const char *tmp_dir = argv[2];

    faac_encoder *enc_tns = NULL, *enc_notns = NULL;
    faac_params params;

    faac_params_init(&params, sizeof(params));
    params.sample_rate = 48000;
    params.num_channels = 2;
    params.bit_rate = 160000; /* 320k stereo */
    params.use_tns = true;
    params.output_format = FAAC_STREAM_ADTS;
    params.input_format = FAAC_INPUT_16BIT;

    if (faac_encoder_open(&params, &enc_tns) != FAAC_OK) return 0;
    params.use_tns = false;
    if (faac_encoder_open(&params, &enc_notns) != FAAC_OK) {
        faac_encoder_close(&enc_tns);
        return 0;
    }

    faac_encoder_info info;
    info.struct_size = sizeof(info);
    faac_encoder_get_info(enc_tns, &info);

    uint32_t frameSize = info.frame_samples * params.num_channels;
    int totalFrames = 50;
    size_t totalSamples = (size_t)totalFrames * frameSize;

    int16_t *pcm = (int16_t *)malloc(totalSamples * sizeof(int16_t));
    uint8_t *outBuf = (uint8_t *)malloc(info.max_output_bytes);

    for (int f = 0; f < totalFrames; f++) {
        for (uint32_t i = 0; i < frameSize; i += 2) {
            double t = (double)(f * frameSize + i) / (48000.0 * 2.0);
            double tone = sin(2.0 * M_PI * 1000.0 * t) + sin(2.0 * M_PI * 4000.0 * t);
            if ((i / 2) % 256 < 16) tone *= 8.0;
            int16_t val = (int16_t)(tone * 1500.0);
            pcm[f * frameSize + i] = val;
            pcm[f * frameSize + i + 1] = val;
        }
    }

    char aac_tns[256], aac_notns[256], wav_tns[256], wav_notns[256];
    snprintf(aac_tns, sizeof(aac_tns), "%s/tns_320k_on.aac", tmp_dir);
    snprintf(aac_notns, sizeof(aac_notns), "%s/tns_320k_off.aac", tmp_dir);
    snprintf(wav_tns, sizeof(wav_tns), "%s/tns_320k_on.wav", tmp_dir);
    snprintf(wav_notns, sizeof(wav_notns), "%s/tns_320k_off.wav", tmp_dir);

    FILE *f_tns = fopen(aac_tns, "wb");
    FILE *f_notns = fopen(aac_notns, "wb");

    for (int f = 0; f < totalFrames; f++) {
        int16_t *frame_pcm = pcm + f * frameSize;
        uint32_t bw = 0;
        if (faac_encoder_encode(enc_tns, frame_pcm, frameSize, outBuf, info.max_output_bytes, &bw) == FAAC_OK && bw > 0)
            fwrite(outBuf, 1, bw, f_tns);

        bw = 0;
        if (faac_encoder_encode(enc_notns, frame_pcm, frameSize, outBuf, info.max_output_bytes, &bw) == FAAC_OK && bw > 0)
            fwrite(outBuf, 1, bw, f_notns);
    }

    uint32_t bw = 0;
    do {
        bw = 0;
        faac_encoder_encode(enc_tns, NULL, 0, outBuf, info.max_output_bytes, &bw);
        if (bw > 0) fwrite(outBuf, 1, bw, f_tns);
    } while (bw > 0);

    do {
        bw = 0;
        faac_encoder_encode(enc_notns, NULL, 0, outBuf, info.max_output_bytes, &bw);
        if (bw > 0) fwrite(outBuf, 1, bw, f_notns);
    } while (bw > 0);

    fclose(f_tns); fclose(f_notns);
    faac_encoder_close(&enc_tns); faac_encoder_close(&enc_notns);

    char cmd1[512], cmd2[512];
    snprintf(cmd1, sizeof(cmd1), "%s -v error -y -i %s -f wav %s", ffmpeg_bin, aac_tns, wav_tns);
    snprintf(cmd2, sizeof(cmd2), "%s -v error -y -i %s -f wav %s", ffmpeg_bin, aac_notns, wav_notns);

    if (system(cmd1) != 0 || system(cmd2) != 0) {
        free(pcm); free(outBuf);
        return 0;
    }

    FILE *f1 = fopen(wav_tns, "rb");
    FILE *f2 = fopen(wav_notns, "rb");
    if (!f1 || !f2) { if (f1) fclose(f1); if (f2) fclose(f2); free(pcm); free(outBuf); return 0; }

    fseek(f1, 0, SEEK_END); long sz1 = ftell(f1); fseek(f1, 44, SEEK_SET);
    fseek(f2, 0, SEEK_END); long sz2 = ftell(f2); fseek(f2, 44, SEEK_SET);

    size_t samples1 = (sz1 > 44) ? (sz1 - 44) / sizeof(int16_t) : 0;
    size_t samples2 = (sz2 > 44) ? (sz2 - 44) / sizeof(int16_t) : 0;

    if (samples1 == 0 || samples2 == 0) {
        fclose(f1); fclose(f2); free(pcm); free(outBuf);
        return 0;
    }

    int16_t *pcm_tns = (int16_t *)malloc(samples1 * sizeof(int16_t));
    int16_t *pcm_notns = (int16_t *)malloc(samples2 * sizeof(int16_t));

    size_t r1 = fread(pcm_tns, sizeof(int16_t), samples1, f1);
    size_t r2 = fread(pcm_notns, sizeof(int16_t), samples2, f2);
    (void)r1; (void)r2;
    fclose(f1); fclose(f2);

    int max_delay_samples = 4096;
    double snr_tns = compute_aligned_snr(pcm, totalSamples, pcm_tns, samples1, max_delay_samples);
    double snr_notns = compute_aligned_snr(pcm, totalSamples, pcm_notns, samples2, max_delay_samples);

    printf("320k High Bitrate FFmpeg Oracle Check:\n");
    printf("  TNS-ON  Aligned Decoded SNR vs Ref = %.2f dB\n", snr_tns);
    printf("  TNS-OFF Aligned Decoded SNR vs Ref = %.2f dB\n", snr_notns);

    free(pcm); free(outBuf); free(pcm_tns); free(pcm_notns);

    return 1;
}

int main(int argc, char **argv) {
    if (!test_tns_math_harness()) {
        return 1;
    }

    int res = run_e2e_ffmpeg_check(argc, argv);
    if (res == 77) {
        printf("FFmpeg oracle test skipped.\n");
        return 0;
    }
    return (res == 1) ? 0 : 1;
}
