#!/usr/bin/env python3
"""
Test script to verify and prove 24-bit 96 kHz bandwidth handling in FAAC.
Generates 24-bit 96 kHz WAV audio files with multi-tone frequencies,
encodes them with faac, decodes with ffmpeg, and performs FFT spectral analysis.
"""

import os
import subprocess
import tempfile
import unittest
import numpy as np
import soundfile as sf


FAAC_BIN = os.path.abspath("./build/frontend/faac")


def generate_96k_24bit_multitone(filename, duration=2.0, frequencies=[5000, 10000, 20000, 30000, 40000]):
    sr = 96000
    t = np.linspace(0, duration, int(sr * duration), endpoint=False)
    signal = np.zeros_like(t, dtype=np.float64)
    for f in frequencies:
        signal += 0.15 * np.sin(2 * np.pi * f * t)

    # Scale to 24-bit signed integer range [-8388608, 8388607]
    signal_24bit = np.clip(signal * 8388607.0, -8388608, 8388607).astype(np.int32)

    # Write as 24-bit PCM WAV using soundfile (mono)
    sf.write(filename, signal_24bit / 8388608.0, sr, subtype='PCM_24')


def generate_24bit_low_level(filename, duration=2.0, frequency=1000, dbfs=-80.0):
    sr = 96000
    t = np.linspace(0, duration, int(sr * duration), endpoint=False)
    amplitude = 10.0 ** (dbfs / 20.0)
    signal = amplitude * np.sin(2 * np.pi * frequency * t)
    sf.write(filename, signal, sr, subtype='PCM_24')


def decode_aac_to_wav(aac_file, wav_file):
    cmd = ["ffmpeg", "-y", "-i", aac_file, "-c:a", "pcm_s24le", wav_file]
    subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)


def get_peak_frequencies_and_energies(wav_file):
    data, sr = sf.read(wav_file)
    if data.ndim > 1:
        data = data[:, 0]

    # Compute FFT
    fft_vals = np.abs(np.fft.rfft(data))
    freqs = np.fft.rfftfreq(len(data), 1.0 / sr)
    return freqs, fft_vals


class Test96k24bitBandwidth(unittest.TestCase):

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.input_wav = os.path.join(self.temp_dir.name, "input_96k_24bit.wav")
        generate_96k_24bit_multitone(self.input_wav)

    def tearDown(self):
        self.temp_dir.cleanup()

    def _encode_and_analyze(self, extra_args):
        aac_out = os.path.join(self.temp_dir.name, "output.m4a")
        cmd = [FAAC_BIN, "-o", aac_out] + extra_args + [self.input_wav]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(res.returncode, 0, f"faac failed: {res.stderr}")

        decoded_wav = os.path.join(self.temp_dir.name, "decoded.wav")
        decode_aac_to_wav(aac_out, decoded_wav)

        freqs, fft_vals = get_peak_frequencies_and_energies(decoded_wav)
        return freqs, fft_vals

    def _get_energy_at_freq(self, freqs, fft_vals, target_freq, window_hz=200):
        mask = (freqs >= target_freq - window_hz) & (freqs <= target_freq + window_hz)
        return np.max(fft_vals[mask]) if np.any(mask) else 0.0

    def test_vbr_default_bandwidth(self):
        """Default VBR at 96 kHz should retain frequencies up to Nyquist (48 kHz)."""
        freqs, fft_vals = self._encode_and_analyze([])

        e_5k = self._get_energy_at_freq(freqs, fft_vals, 5000)
        e_10k = self._get_energy_at_freq(freqs, fft_vals, 10000)
        e_20k = self._get_energy_at_freq(freqs, fft_vals, 20000)
        e_30k = self._get_energy_at_freq(freqs, fft_vals, 30000)
        e_40k = self._get_energy_at_freq(freqs, fft_vals, 40000)

        # In default high-quality VBR at 96 kHz, tones at 20 kHz, 30 kHz, 40 kHz must be preserved
        self.assertGreater(e_5k, 10.0, "5 kHz tone missing")
        self.assertGreater(e_10k, 10.0, "10 kHz tone missing")
        self.assertGreater(e_20k, 5.0, "20 kHz tone missing in high-rate VBR")
        self.assertGreater(e_30k, 5.0, "30 kHz tone missing in high-rate VBR")
        self.assertGreater(e_40k, 5.0, "40 kHz tone missing in high-rate VBR")

    def test_high_bitrate_abr_bandwidth(self):
        """High bitrate ABR (160 kbps/ch) at 96 kHz should retain high frequencies up to Nyquist."""
        freqs, fft_vals = self._encode_and_analyze(["-b", "160"])  # 160 kbps/ch for mono

        e_20k = self._get_energy_at_freq(freqs, fft_vals, 20000)
        e_30k = self._get_energy_at_freq(freqs, fft_vals, 30000)
        e_40k = self._get_energy_at_freq(freqs, fft_vals, 40000)

        self.assertGreater(e_20k, 5.0, "20 kHz tone missing at high bitrate")
        self.assertGreater(e_30k, 5.0, "30 kHz tone missing at high bitrate")
        self.assertGreater(e_40k, 5.0, "40 kHz tone missing at high bitrate")

    def test_low_bitrate_abr_bandwidth(self):
        """Low bitrate ABR (48 kbps/ch) should enforce ~18.75 kHz cutoff."""
        freqs, fft_vals = self._encode_and_analyze(["-b", "48"])  # 48 kbps/ch

        e_5k = self._get_energy_at_freq(freqs, fft_vals, 5000)
        e_10k = self._get_energy_at_freq(freqs, fft_vals, 10000)
        e_30k = self._get_energy_at_freq(freqs, fft_vals, 30000)
        e_40k = self._get_energy_at_freq(freqs, fft_vals, 40000)

        self.assertGreater(e_5k, 10.0, "5 kHz tone missing")
        self.assertGreater(e_10k, 10.0, "10 kHz tone missing")
        # 30 kHz and 40 kHz should be cut off at 48 kbps/ch
        self.assertLess(e_30k, 1.0, "30 kHz tone should be cut off at low bitrate")
        self.assertLess(e_40k, 1.0, "40 kHz tone should be cut off at low bitrate")

    def test_explicit_bandwidth_override(self):
        """Explicit -c 24000 cutoff override."""
        freqs, fft_vals = self._encode_and_analyze(["-c", "24000"])

        e_20k = self._get_energy_at_freq(freqs, fft_vals, 20000)
        e_30k = self._get_energy_at_freq(freqs, fft_vals, 30000)

        self.assertGreater(e_20k, 5.0, "20 kHz tone missing below 24 kHz cutoff")
        self.assertLess(e_30k, 1.0, "30 kHz tone should be cut off above 24 kHz cutoff")

    def test_24bit_low_level_precision(self):
        """Verify 24-bit low-level signal (-80 dBFS) is encoded and decoded without noise floor collapse."""
        low_level_wav = os.path.join(self.temp_dir.name, "input_low.wav")
        generate_24bit_low_level(low_level_wav)

        aac_out = os.path.join(self.temp_dir.name, "low_output.m4a")
        cmd = [FAAC_BIN, "-o", aac_out, low_level_wav]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(res.returncode, 0)

        decoded_wav = os.path.join(self.temp_dir.name, "low_decoded.wav")
        decode_aac_to_wav(aac_out, decoded_wav)

        freqs, fft_vals = get_peak_frequencies_and_energies(decoded_wav)
        e_1k = self._get_energy_at_freq(freqs, fft_vals, 1000)
        self.assertGreater(e_1k, 0.0001, "Low-level -80 dBFS 1 kHz tone missing or collapsed")


if __name__ == "__main__":
    unittest.main()
