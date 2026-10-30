#!/usr/bin/env python3
"""
Standalone FDK-AAC DRC Validation Script for FAAC.

Validates FAAC's dynamic range control (DRC) bitstream encoding against the
reference Fraunhofer FDK-AAC decoder (libfdk-aac).

Usage:
  python3 experiments/scripts/validate_drc_fdk.py [--faac PATH] [--faad PATH]
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import numpy as np
import soundfile as sf

FDK_PROBE_C = r"""/* Standalone FDK-AAC DRC Decoder Probe */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "aacdecoder_lib.h"

static void write_le(uint8_t *p, uint32_t val, int bytes) {
    for (int i = 0; i < bytes; i++) p[i] = (val >> (8 * i)) & 0xFF;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s in.aac out.wav cut(0..127) boost(0..127) target(0=off|40..127)\n", argv[0]);
        return 2;
    }
    const char *in_file = argv[1];
    const char *out_file = argv[2];
    int cut = atoi(argv[3]);
    int boost = atoi(argv[4]);
    int target = atoi(argv[5]);

    FILE *f = fopen(in_file, "rb");
    if (!f) { perror("fopen in"); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    UCHAR *buf = (UCHAR *)malloc(len);
    if (!buf || fread(buf, 1, len, f) != (size_t)len) {
        fclose(f);
        return 1;
    }
    fclose(f);

    HANDLE_AACDECODER d = aacDecoder_Open(TT_MP4_ADTS, 1);
    if (!d) return 1;

    aacDecoder_SetParam(d, AAC_PCM_LIMITER_ENABLE, 0);
    aacDecoder_SetParam(d, AAC_DRC_ATTENUATION_FACTOR, cut);
    aacDecoder_SetParam(d, AAC_DRC_BOOST_FACTOR, boost);
    if (target > 0)
        aacDecoder_SetParam(d, AAC_DRC_REFERENCE_LEVEL, target);
    else
        aacDecoder_SetParam(d, AAC_DRC_REFERENCE_LEVEL, -1);

    size_t pcm_cap = 1 << 22;
    size_t pcm_samples = 0;
    int16_t *pcm_buf = (int16_t *)malloc(pcm_cap * sizeof(int16_t));
    INT_PCM out_frame[8 * 2048 * 2];

    int channels = 2, sample_rate = 48000;
    UCHAR *p = buf;
    UINT size = (UINT)len, valid = (UINT)len;

    while (size > 0) {
        if (aacDecoder_Fill(d, &p, &size, &valid) != AAC_DEC_OK) break;
        UINT used = size - valid;
        p += used;
        size = valid;

        for (;;) {
            if (aacDecoder_DecodeFrame(d, out_frame, sizeof(out_frame) / sizeof(out_frame[0]), 0) != AAC_DEC_OK) break;
            CStreamInfo *si = aacDecoder_GetStreamInfo(d);
            channels = si->numChannels;
            sample_rate = si->sampleRate;
            size_t frame_samps = (size_t)si->frameSize * channels;

            if (pcm_samples + frame_samps > pcm_cap) {
                pcm_cap = (pcm_samples + frame_samps) * 2;
                pcm_buf = (int16_t *)realloc(pcm_buf, pcm_cap * sizeof(int16_t));
            }
            memcpy(pcm_buf + pcm_samples, out_frame, frame_samps * sizeof(int16_t));
            pcm_samples += frame_samps;
        }
    }

    aacDecoder_Close(d);
    free(buf);

    FILE *o = fopen(out_file, "wb");
    if (!o) { free(pcm_buf); return 1; }

    uint8_t hdr[44];
    uint32_t data_bytes = (uint32_t)(pcm_samples * sizeof(int16_t));
    memcpy(hdr, "RIFF", 4);
    write_le(hdr + 4, 36 + data_bytes, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    write_le(hdr + 16, 16, 4);
    write_le(hdr + 20, 1, 2);
    write_le(hdr + 22, channels, 2);
    write_le(hdr + 24, sample_rate, 4);
    write_le(hdr + 28, sample_rate * channels * 2, 4);
    write_le(hdr + 32, channels * 2, 2);
    write_le(hdr + 34, 16, 2);
    memcpy(hdr + 36, "data", 4);
    write_le(hdr + 40, data_bytes, 4);

    fwrite(hdr, 1, 44, o);
    fwrite(pcm_buf, sizeof(int16_t), pcm_samples, o);
    fclose(o);
    free(pcm_buf);

    return 0;
}
"""

def compile_fdk_probe(work_dir):
    c_path = os.path.join(work_dir, "fdkdrc.c")
    bin_path = os.path.join(work_dir, "fdkdrc")
    with open(c_path, "w") as f:
        f.write(FDK_PROBE_C)

    inc_dir = "/usr/include/fdk-aac"
    cmd = ["gcc", "-O2", "-I" + inc_dir, c_path, "-lfdk-aac", "-o", bin_path]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit(f"Failed to compile FDK probe:\n{res.stderr}")
    return bin_path


def make_test_wav(path, sr=48000, duration=6.0):
    t = np.linspace(0, duration, int(sr * duration), endpoint=False)
    split = int(sr * duration / 2)
    # Quiet 1 kHz tone (-30 dBFS) followed by loud 1 kHz tone (0 dBFS)
    quiet = 0.0316 * np.sin(2 * np.pi * 1000 * t[:split])
    loud = 0.99 * np.sin(2 * np.pi * 1000 * t[split:])
    sig = np.concatenate([quiet, loud])
    stereo = np.column_stack([sig, sig])
    sf.write(path, stereo, sr, subtype="PCM_16")


def main():
    parser = argparse.ArgumentParser(description="FAAC DRC validation against FDK-AAC")
    parser.add_argument("--faac", default="build_test/frontend/faac", help="Path to faac binary")
    parser.add_argument("--faad", default="build_test/frontend/faad", help="Path to faad binary")
    args = parser.parse_args()

    faac = os.path.abspath(args.faac)
    faad = os.path.abspath(args.faad)

    if not os.path.exists(faac):
        sys.exit(f"FAAC executable not found at {faac}. Please build FAAC first.")

    with tempfile.TemporaryDirectory() as td:
        print("Compiling standalone FDK-AAC probe executable...")
        fdk_bin = compile_fdk_probe(td)

        in_wav = os.path.join(td, "test_input.wav")
        aac_out = os.path.join(td, "test_drc.aac")
        make_test_wav(in_wav)

        print("\n1. Encoding test PCM to AAC with FAAC (--drc film --drc-ref-level -23)...")
        enc_cmd = [faac, "-a", "--drc", "film", "--drc-ref-level", "-23", in_wav, "-o", aac_out]
        enc_res = subprocess.run(enc_cmd, capture_output=True, text=True)
        if enc_res.returncode != 0:
            sys.exit(f"FAAC encoding failed:\n{enc_res.stderr}")
        print("   Encoding successful.")

        print("\n2. Decoding with FDK-AAC (DRC Off vs DRC On)...")
        fdk_wav_off = os.path.join(td, "fdk_off.wav")
        fdk_wav_on = os.path.join(td, "fdk_on.wav")

        # FDK-AAC DRC off: cut=0, boost=0, target=0
        r_off = subprocess.run([fdk_bin, aac_out, fdk_wav_off, "0", "0", "0"])
        # FDK-AAC DRC on: cut=127, boost=127, target=96 (-24 dBFS)
        r_on = subprocess.run([fdk_bin, aac_out, fdk_wav_on, "127", "127", "96"])

        if r_off.returncode != 0 or r_on.returncode != 0:
            sys.exit("FDK-AAC decoding failed.")

        x_off, sr = sf.read(fdk_wav_off)
        x_on, _ = sf.read(fdk_wav_on)

        mid = len(x_off) // 2
        rms_quiet_off = float(np.sqrt(np.mean(x_off[:mid]**2)))
        rms_quiet_on = float(np.sqrt(np.mean(x_on[:mid]**2)))
        rms_loud_off = float(np.sqrt(np.mean(x_off[mid:]**2)))
        rms_loud_on = float(np.sqrt(np.mean(x_on[mid:]**2)))

        loud_attenuation_db = 20.0 * np.log10(rms_loud_on / rms_loud_off)
        quiet_boost_db = 20.0 * np.log10(rms_quiet_on / rms_quiet_off)

        print(f"   FDK-AAC Quiet passage RMS:  Off = {rms_quiet_off:.4f}, On = {rms_quiet_on:.4f} ({quiet_boost_db:+.2f} dB)")
        print(f"   FDK-AAC Loud passage RMS:   Off = {rms_loud_off:.4f}, On = {rms_loud_on:.4f} ({loud_attenuation_db:+.2f} dB)")

        assert loud_attenuation_db < -10.0, f"Expected >10 dB cut on loud passage under film DRC profile, got {loud_attenuation_db:.2f} dB"

        print("\n3. Comparing FDK-AAC and FAAD DRC decoded outputs...")
        if os.path.exists(faad):
            faad_wav_on = os.path.join(td, "faad_on.wav")
            faad_cmd = [faad, "--drc-cut", "127", "--drc-boost", "127", "--drc-target", "96", "-o", faad_wav_on, aac_out]
            subprocess.run(faad_cmd, capture_output=True, check=True)

            x_faad, _ = sf.read(faad_wav_on)
            min_len = min(len(x_on), len(x_faad))
            diff = x_on[:min_len] - x_faad[:min_len]
            max_abs_diff = float(np.max(np.abs(diff)))
            snr_db = float(20 * np.log10(np.sqrt(np.mean(x_on[:min_len]**2)) / (np.sqrt(np.mean(diff**2)) + 1e-12)))

            print(f"   Max abs sample diff between FDK-AAC and FAAD: {max_abs_diff:.6f}")
            print(f"   Signal-to-Noise Ratio (FDK-AAC vs FAAD):       {snr_db:.2f} dB")
            assert snr_db > 60.0, f"Expected SNR > 60 dB between decoders, got {snr_db:.2f} dB"

        print("\n=== DRC Validation Successful! ===")
        print("FAAC's ISO/IEC 14496-3 dynamic_range_info payload was correctly parsed and applied by FDK-AAC.")


if __name__ == "__main__":
    main()
