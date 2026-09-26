#!/usr/bin/env python3
"""
Bitstream Identity Verification Script for FAAC Joint-RD Optimization.
Validates bitstream byte-identity across at least 12 inputs x 8 encoding modes.
"""

import sys
import os
import wave
import math
import struct
import random
import argparse
import subprocess
import filecmp

TEST_WAV_DIR = "/tmp/faac_test_wavs"

MODES = [
    ("ABR_64", ["-b", "64"]),
    ("ABR_128", ["-b", "128"]),
    ("ABR_192", ["-b", "192"]),
    ("CBR_128", ["-b", "128", "--cbr"]),
    ("VBR_50", ["-q", "50"]),
    ("VBR_100", ["-q", "100"]),
    ("HE_32", ["-b", "32", "--object-type", "he-aac-v1"]),
    ("MONO_24", ["-b", "24"]),
]

def generate_test_wavs():
    os.makedirs(TEST_WAV_DIR, exist_ok=True)
    wav_files = []

    # 1. Silence (48kHz stereo)
    p = os.path.join(TEST_WAV_DIR, "01_silence.wav")
    with wave.open(p, "w") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(48000)
        f.writeframes(struct.pack("<" + "h" * (48000 * 2 * 2), *([0] * (48000 * 2 * 2))))
    wav_files.append(("silence", p, False))

    # 2. Tonal (48kHz stereo)
    p = os.path.join(TEST_WAV_DIR, "02_tonal.wav")
    with wave.open(p, "w") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(48000)
        samples = []
        for i in range(48000 * 2):
            t = i / 48000.0
            l = int(16000 * math.sin(2 * math.pi * 440 * t) + 8000 * math.sin(2 * math.pi * 880 * t))
            r = int(16000 * math.sin(2 * math.pi * 554.37 * t) + 8000 * math.sin(2 * math.pi * 1108.73 * t))
            samples.extend([max(-32768, min(32767, l)), max(-32768, min(32767, r))])
        f.writeframes(struct.pack("<" + "h" * len(samples), *samples))
    wav_files.append(("tonal", p, False))

    # 3. Noise (48kHz stereo)
    p = os.path.join(TEST_WAV_DIR, "03_noise.wav")
    random.seed(42)
    with wave.open(p, "w") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(48000)
        samples = [int(random.uniform(-16000, 16000)) for _ in range(48000 * 2 * 2)]
        f.writeframes(struct.pack("<" + "h" * len(samples), *samples))
    wav_files.append(("noise", p, False))

    # 4. Transient (48kHz stereo)
    p = os.path.join(TEST_WAV_DIR, "04_transient.wav")
    with wave.open(p, "w") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(48000)
        samples = []
        for i in range(48000 * 2):
            v = 28000 if (i % 4800 < 50) else 0
            samples.extend([v, v])
        f.writeframes(struct.pack("<" + "h" * len(samples), *samples))
    wav_files.append(("transient", p, False))

    # 5. Speech-synth (48kHz stereo)
    p = os.path.join(TEST_WAV_DIR, "05_speech_synth.wav")
    with wave.open(p, "w") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(48000)
        samples = []
        for i in range(48000 * 2):
            t = i / 48000.0
            f0 = 150 + 30 * math.sin(2 * math.pi * 3 * t)
            v = int(12000 * math.sin(2 * math.pi * f0 * t) + 6000 * math.sin(2 * math.pi * 2 * f0 * t))
            samples.extend([max(-32768, min(32767, v)), max(-32768, min(32767, v))])
        f.writeframes(struct.pack("<" + "h" * len(samples), *samples))
    wav_files.append(("speech_synth", p, False))

    # External WAVs
    ext_files = [
        ("german_speech", "/opt/faac-benchmark/data/external/audio/12-German-male-speech.441.16b48k.wav", False),
        ("glockenspiel", "/opt/faac-benchmark/data/external/audio/35_SQAM_glockenspiel_cut.16b48k.wav", False),
        ("classic", "/opt/faac-benchmark/data/external/audio/21-classic.441.16b48k.wav", False),
        ("drums", "/opt/faac-benchmark/data/external/audio/27-last-song-drums-and-trampets.441.16b48k.wav", False),
        ("hotel_california", "/opt/faac-benchmark/data/external/audio/Hotel California.16b48k.wav", False),
        ("bohemian_rhapsody", "/opt/faac-benchmark/data/external/audio/bonhemian_rhapsody.16b48k.wav", False),
    ]

    for name, path, is_mono in ext_files:
        if os.path.exists(path):
            wav_files.append((name, path, is_mono))

    # 12. Mono Speech (48kHz mono)
    p = os.path.join(TEST_WAV_DIR, "12_mono_48k.wav")
    with wave.open(p, "w") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(48000)
        samples = []
        for i in range(48000 * 2):
            t = i / 48000.0
            v = int(16000 * math.sin(2 * math.pi * 300 * t) + 8000 * math.sin(2 * math.pi * 1200 * t))
            samples.append(max(-32768, min(32767, v)))
        f.writeframes(struct.pack("<" + "h" * len(samples), *samples))
    wav_files.append(("mono_48k", p, True))

    return wav_files

def get_mode_opts(mode_name, default_opts, is_mono):
    if is_mono:
        if mode_name == "ABR_128": return ["-b", "32"]
        if mode_name == "ABR_192": return ["-b", "48"]
        if mode_name == "CBR_128": return ["-b", "32", "--cbr"]
        if mode_name == "HE_32": return ["-b", "24", "--object-type", "he-aac-v1"]
    return default_opts

def run_encode(faac_bin, wav_path, out_aac, opts):
    cmd = [faac_bin, "-v", "0", "--overwrite"] + opts + [wav_path, "-o", out_aac]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"Encode failed: {' '.join(cmd)}\n{res.stderr}")

def main():
    parser = argparse.ArgumentParser(description="Verify bitstream identity across FAAC modes.")
    parser.add_argument("--faac", required=True, help="Path to faac executable")
    parser.add_argument("--generate-ref", help="Output directory to store reference AAC bitstreams")
    parser.add_argument("--verify-ref", help="Directory containing reference AAC bitstreams to verify against")
    args = parser.parse_args()

    wavs = generate_test_wavs()

    if args.generate_ref:
        os.makedirs(args.generate_ref, exist_ok=True)
        print(f"Generating reference bitstreams in {args.generate_ref} using {args.faac}...")
        count = 0
        for name, path, is_mono in wavs:
            for mode_name, default_opts in MODES:
                opts = get_mode_opts(mode_name, default_opts, is_mono)
                out_path = os.path.join(args.generate_ref, f"{name}_{mode_name}.aac")
                run_encode(args.faac, path, out_path, opts)
                count += 1
        print(f"Successfully generated {count} reference AAC files.")
        sys.exit(0)

    if args.verify_ref:
        test_out_dir = "/tmp/faac_test_output"
        os.makedirs(test_out_dir, exist_ok=True)
        print(f"Verifying bitstream byte-identity against {args.verify_ref} using {args.faac}...")

        results = []
        all_passed = True
        total = 0
        passed = 0

        # Header for Markdown table
        mode_names = [m[0] for m in MODES]

        for name, path, is_mono in wavs:
            row = {"input": name}
            for mode_name, default_opts in MODES:
                total += 1
                opts = get_mode_opts(mode_name, default_opts, is_mono)
                ref_path = os.path.join(args.verify_ref, f"{name}_{mode_name}.aac")
                test_path = os.path.join(test_out_dir, f"{name}_{mode_name}.aac")

                if not os.path.exists(ref_path):
                    row[mode_name] = "MISSING_REF"
                    all_passed = False
                    continue

                try:
                    run_encode(args.faac, path, test_path, opts)
                    if filecmp.cmp(ref_path, test_path, shallow=False):
                        row[mode_name] = "PASS"
                        passed += 1
                    else:
                        row[mode_name] = "FAIL"
                        all_passed = False
                        print(f"BITSTREAM MISMATCH: {name} {mode_name}")
                except Exception as e:
                    row[mode_name] = "ERROR"
                    all_passed = False
                    print(f"ERROR: {name} {mode_name}: {e}")

            results.append(row)

        print("\n--- Bitstream Identity Pass/Fail Table ---")
        headers = ["Input Signal"] + mode_names
        print("| " + " | ".join(headers) + " |")
        print("| " + " | ".join(["---"] * len(headers)) + " |")
        for row in results:
            line = [row["input"]] + [row[m] for m in mode_names]
            print("| " + " | ".join(line) + " |")

        print(f"\nSummary: {passed}/{total} passed.")
        if not all_passed:
            print("FAILURE: Bitstream mismatch detected!")
            sys.exit(1)
        else:
            print("SUCCESS: 100% bitstream byte-identity confirmed across all inputs and modes.")
            sys.exit(0)

if __name__ == "__main__":
    main()
