#!/usr/bin/env python3
"""Check FAAC's High-quality policy for resampled input above 48 kHz.

Usage: test_faac_resample_cli.py <faac> <faad>
"""

import math
import os
import struct
import subprocess
import sys
import tempfile
import wave


faac, faad = map(os.path.abspath, sys.argv[1:3])
cases = ((64000, 48000), (88200, 44100), (96000, 48000))

with tempfile.TemporaryDirectory() as tmp:
    for input_rate, output_rate in cases:
        source = os.path.join(tmp, f"input-{input_rate}.wav")
        encoded = os.path.join(tmp, f"output-{input_rate}.aac")
        decoded = os.path.join(tmp, f"decoded-{input_rate}.wav")
        with wave.open(source, "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(input_rate)
            pcm = b"".join(
                struct.pack("<h", int(12000 * math.sin(2 * math.pi * 440 * i / input_rate)))
                for i in range(input_rate)
            )
            wav.writeframes(pcm)

        result = subprocess.run(
            [faac, "--rate", "auto", "--object-type", "lc", "-b", "128",
             "-a", "--overwrite", "-o", encoded, source],
            capture_output=True,
            text=True,
        )
        output = result.stdout + result.stderr
        if result.returncode:
            print(output, file=sys.stderr)
            raise SystemExit(f"FAAC failed unexpectedly for {input_rate} Hz input")

        expected = f"Resampling input: {input_rate} Hz -> {output_rate} Hz (FAAR High)"
        if expected not in output:
            raise SystemExit(f"missing High-profile report: {output}")

        decoded_result = subprocess.run(
            [faad, "--quiet", "--overwrite", "-o", decoded, encoded],
            capture_output=True,
            text=True,
        )
        if decoded_result.returncode:
            print(decoded_result.stderr, file=sys.stderr)
            raise SystemExit(f"FAAD could not decode the {input_rate} Hz test stream")
        with wave.open(decoded, "rb") as wav:
            if (wav.getframerate(), wav.getnchannels()) != (output_rate, 1):
                raise SystemExit(
                    f"unexpected decoded format for {input_rate} Hz input: "
                    f"{wav.getframerate()} Hz, {wav.getnchannels()} channels"
                )

print("Resampling above 48 kHz uses FAAR High and decodes at the selected rate")
