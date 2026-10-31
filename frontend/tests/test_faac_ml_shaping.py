#!/usr/bin/env python3
"""
Integration test for FAAC ML Audio Shaping & Telemetry logging.
"""

import os
import sys
import subprocess
import tempfile
import unittest
import wave
import struct
import random

class TestFaacMlShaping(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Allow passing faac binary and max_channels as CLI arguments from meson
        if len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
            cls.faac_bin = os.path.abspath(sys.argv[1])
            cls.max_channels = int(sys.argv[2]) if len(sys.argv) > 2 else 2
        else:
            cls.faac_bin = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "build", "frontend", "faac"))
            if not os.path.exists(cls.faac_bin):
                cls.faac_bin = os.path.abspath("build/frontend/faac")
            cls.max_channels = 2

    def test_cli_log_shaping(self):
        channels = 1 if self.max_channels < 2 else 2
        with tempfile.TemporaryDirectory() as tmpdir:
            wav_path = os.path.join(tmpdir, "in.wav")
            m4a_path = os.path.join(tmpdir, "out.m4a")
            csv_path = os.path.join(tmpdir, "shaping.csv")

            with wave.open(wav_path, 'w') as f:
                f.setnchannels(channels)
                f.setsampwidth(2)
                f.setframerate(44100)
                if channels == 1:
                    samples = [struct.pack('<h', random.randint(-15000, 15000)) for _ in range(44100)]
                else:
                    samples = [struct.pack('<hh', random.randint(-15000, 15000), random.randint(-15000, 15000)) for _ in range(44100)]
                f.writeframes(b''.join(samples))

            cmd = [self.faac_bin, "--overwrite", wav_path, "-o", m4a_path, "--log-shaping", csv_path]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertEqual(res.returncode, 0, f"FAAC failed: {res.stderr}")
            self.assertTrue(os.path.exists(csv_path), "CSV telemetry file not created")

            with open(csv_path, 'r', encoding='utf-8') as f:
                lines = [line.strip() for line in f if line.strip()]
            self.assertGreater(len(lines), 5, "Telemetry CSV has too few lines")
            self.assertTrue(lines[0].startswith("frame_idx,channel,num_channels"), "CSV header mismatch")

    def test_env_var_log_shaping(self):
        channels = 1 if self.max_channels < 2 else 2
        with tempfile.TemporaryDirectory() as tmpdir:
            wav_path = os.path.join(tmpdir, "in.wav")
            m4a_path = os.path.join(tmpdir, "out.m4a")
            csv_path = os.path.join(tmpdir, "env_shaping.csv")

            with wave.open(wav_path, 'w') as f:
                f.setnchannels(channels)
                f.setsampwidth(2)
                f.setframerate(44100)
                if channels == 1:
                    samples = [struct.pack('<h', random.randint(-10000, 10000)) for _ in range(44100)]
                else:
                    samples = [struct.pack('<hh', random.randint(-10000, 10000), random.randint(-10000, 10000)) for _ in range(44100)]
                f.writeframes(b''.join(samples))

            env = os.environ.copy()
            env["FAAC_LOG_SHAPING"] = csv_path
            cmd = [self.faac_bin, "--overwrite", wav_path, "-o", m4a_path]
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=True)
            self.assertEqual(res.returncode, 0, f"FAAC failed: {res.stderr}")
            self.assertTrue(os.path.exists(csv_path), "CSV telemetry file via ENV not created")

            with open(csv_path, 'r', encoding='utf-8') as f:
                lines = [line.strip() for line in f if line.strip()]
            self.assertGreater(len(lines), 5)

if __name__ == '__main__':
    # Strip custom command-line arguments so unittest.main doesn't treat them as test names
    unittest.main(argv=sys.argv[:1])
