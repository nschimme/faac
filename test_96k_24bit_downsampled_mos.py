#!/usr/bin/env python3
"""
Test script to perform downsampled 48 kHz 16-bit MOS / spectral validation
of 24-bit 96 kHz audio encoded with FAAC.
"""

import os
import subprocess
import tempfile
import unittest
import numpy as np
import soundfile as sf

FAAC_BIN = os.path.abspath("./build/frontend/faac")


def generate_complex_96k_24bit_signal(filename, duration=3.0):
    sr = 96000
    t = np.linspace(0, duration, int(sr * duration), endpoint=False)

    # Fundamental tones in the audible range (0 - 20 kHz)
    s = 0.2 * np.sin(2 * np.pi * 440 * t) + \
        0.15 * np.sin(2 * np.pi * 1000 * t) + \
        0.10 * np.sin(2 * np.pi * 5000 * t) + \
        0.08 * np.sin(2 * np.pi * 12000 * t) + \
        0.05 * np.sin(2 * np.pi * 18000 * t)

    # Ultrasonic tones (25 kHz, 35 kHz)
    s += 0.05 * np.sin(2 * np.pi * 25000 * t) + \
         0.03 * np.sin(2 * np.pi * 35000 * t)

    sf.write(filename, s, sr, subtype='PCM_24')


def resample_to_48k_16bit(in_wav, out_wav):
    cmd = [
        "ffmpeg", "-y", "-i", in_wav,
        "-af", "aresample=48000:resampler=soxr",
        "-c:a", "pcm_s16le", out_wav
    ]
    subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)


class TestDownsampled48kMOS(unittest.TestCase):

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.ref_96k = os.path.join(self.temp_dir.name, "ref_96k.wav")
        generate_complex_96k_24bit_signal(self.ref_96k)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_downsampled_48k_quality(self):
        """Verify 0-24 kHz audio quality when downsampled to 48 kHz 16-bit."""
        aac_file = os.path.join(self.temp_dir.name, "output.m4a")
        cmd = [FAAC_BIN, "-o", aac_file, self.ref_96k]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(res.returncode, 0, f"faac failed: {res.stderr}")

        dec_96k = os.path.join(self.temp_dir.name, "dec_96k.wav")
        subprocess.run(["ffmpeg", "-y", "-i", aac_file, "-c:a", "pcm_s24le", dec_96k],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)

        ref_48k = os.path.join(self.temp_dir.name, "ref_48k.wav")
        dec_48k = os.path.join(self.temp_dir.name, "dec_48k.wav")

        resample_to_48k_16bit(self.ref_96k, ref_48k)
        resample_to_48k_16bit(dec_96k, dec_48k)

        ref_data, sr_ref = sf.read(ref_48k)
        dec_data, sr_dec = sf.read(dec_48k)

        self.assertEqual(sr_ref, 48000)
        self.assertEqual(sr_dec, 48000)

        # Truncate to matching length
        min_len = min(len(ref_data), len(dec_data))
        ref_data = ref_data[:min_len]
        dec_data = dec_data[:min_len]

        # Compute SNR in 0-24 kHz band
        error = ref_data - dec_data
        signal_power = np.mean(ref_data ** 2)
        noise_power = np.mean(error ** 2)

        snr_db = 10 * np.log10(signal_power / noise_power) if noise_power > 0 else 100.0

        # High-quality VBR should achieve >= 20 dB SNR in downsampled 48 kHz comparison
        self.assertGreater(snr_db, 20.0, f"Downsampled 0-24 kHz SNR too low: {snr_db:.2f} dB")


if __name__ == "__main__":
    unittest.main()
