#!/usr/bin/env python3
"""
E2E test for FAAC ML Audio Shaping Telemetry
"""

import os
import subprocess
import sys
import tempfile
import unittest
import wave


class TestFaacMlShaping(unittest.TestCase):
    def setUp(self):
        self.faac_bin = sys.argv[1] if len(sys.argv) > 1 else 'frontend/faac'
        self.tmp_dir = tempfile.mkdtemp()
        self.wav_path = os.path.join(self.tmp_dir, 'test.wav')
        self.aac_path = os.path.join(self.tmp_dir, 'test.aac')
        self._generate_sine_wav(self.wav_path)

    def tearDown(self):
        for f in [self.wav_path, self.aac_path]:
            if os.path.exists(f):
                os.remove(f)
        if os.path.exists(self.tmp_dir):
            import shutil
            shutil.rmtree(self.tmp_dir)

    def _generate_sine_wav(self, path, duration_s=0.5, rate=44100):
        import math
        import struct
        num_frames = int(duration_s * rate)
        with wave.open(path, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(rate)
            data = bytearray()
            for i in range(num_frames):
                val = int(16000.0 * math.sin(2.0 * math.pi * 1000.0 * i / rate))
                data.extend(struct.pack('<hh', val, val))
            w.writeframes(data)

    def test_env_var_log_shaping(self):
        csv_path = os.path.join(self.tmp_dir, 'env_telemetry.csv')
        env = os.environ.copy()
        env['FAAC_LOG_SHAPING'] = csv_path
        cmd = [self.faac_bin, '-b', '64', '-o', self.aac_path, self.wav_path]
        res = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(res.returncode, 0, f"faac failed: {res.stderr.decode()}")
        self.assertTrue(os.path.exists(csv_path), "CSV telemetry file via ENV not created")
        with open(csv_path, 'r') as f:
            lines = f.readlines()
            self.assertGreater(len(lines), 1, "CSV file is empty or only header")


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
