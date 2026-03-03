"""
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
"""

import os
import subprocess
import time
import sys
import json
import re
import tempfile
import shutil

EXTERNAL_DATA_DIR = "tests/data/external"
OUTPUT_DIR = "tests/output"

SCENARIOS = {
    "voip": {"mode": "speech", "rate": 16000, "visqol_rate": 16000, "q": 15, "thresh": 2.5},
    "nvr": {"mode": "audio", "rate": 48000, "visqol_rate": 48000, "q": 30, "thresh": 3.0},
    "music_low": {"mode": "audio", "rate": 48000, "visqol_rate": 48000, "q": 60, "thresh": 3.5},
    "music_std": {"mode": "audio", "rate": 48000, "visqol_rate": 48000, "q": 120, "thresh": 4.0},
    "music_high": {"mode": "audio", "rate": 48000, "visqol_rate": 48000, "q": 250, "thresh": 4.3}
}

def get_info(wav_path):
    try:
        cmd = ["ffprobe", "-v", "error", "-show_entries", "stream=channels", "-of", "default=noprint_wrappers=1:nokey=1", wav_path]
        res = subprocess.run(cmd, capture_output=True, text=True)
        return int(res.stdout.strip())
    except:
        return 2

def get_binary_size(path):
    if os.path.exists(path):
        return os.path.getsize(path)
    return 0

def run_visqol(ref_wav, deg_wav, mode):
    """Run ViSQOL and parse MOS score."""
    try:
        import visqol_py
        from visqol_py import visqol_lib_py
        from visqol_py.pb2 import visqol_config_pb2
        from visqol_py.pb2 import similarity_result_pb2

        config = visqol_config_pb2.VisqolConfig()
        if mode == "speech":
            config.audio.sample_rate = 16000
            config.options.use_speech_scoring = True
        else:
            config.audio.sample_rate = 48000
            config.options.use_speech_scoring = False
            config.options.full_band_expectation = True
            config.options.similarity_to_quality_model = "model/libsvm_nu_svr_model.txt"

        api = visqol_lib_py.VisqolApi()
        api.Create(config)

        # Load audio files
        def read_wav(path):
            import wave
            import numpy as np
            with wave.open(path, 'rb') as wf:
                params = wf.getparams()
                frames = wf.readframes(params.nframes)
                return np.frombuffer(frames, dtype=np.int16).astype(np.float)

        ref_audio = read_wav(ref_wav)
        deg_audio = read_wav(deg_wav)

        # ViSQOL expects 1D arrays
        result = api.Measure(ref_audio.tolist(), deg_audio.tolist())
        return result.moslqo
    except Exception as e:
        print(f"ViSQOL error: {e}")
        pass
    return None

def run_benchmark(faac_path, lib_path, precision, coverage=100, run_perceptual=False):
    env = os.environ.copy()

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    results = {
        "matrix": {},
        "throughput": {},
        "lib_size": get_binary_size(lib_path)
    }

    if run_perceptual:
        for name, cfg in SCENARIOS.items():
            data_subdir = "speech" if cfg["mode"] == "speech" else "audio"
            data_dir = os.path.join(EXTERNAL_DATA_DIR, data_subdir)
            if not os.path.exists(data_dir): continue

            all_samples = sorted([f for f in os.listdir(data_dir) if f.endswith(".wav")])
            num_to_run = max(1, int(len(all_samples) * coverage / 100.0))
            step = len(all_samples) / num_to_run
            samples = [all_samples[int(i * step)] for i in range(num_to_run)]

            for sample in samples:
                input_path = os.path.join(data_dir, sample)
                key = f"{name}_{sample}"
                output_path = os.path.join(OUTPUT_DIR, f"{key}_{precision}.aac")

                try:
                    subprocess.run([faac_path, "-q", str(cfg["q"]), "-o", output_path, input_path],
                                   env=env, check=True, capture_output=True)

                    mos = None
                    aac_size = os.path.getsize(output_path)

                    with tempfile.TemporaryDirectory() as tmpdir:
                        deg_wav = os.path.join(tmpdir, "deg.wav")
                        subprocess.run(["faad", "-o", deg_wav, output_path], capture_output=True)
                        if os.path.exists(deg_wav):
                            v_rate = cfg["visqol_rate"]
                            v_ref = os.path.join(tmpdir, "vref.wav")
                            v_deg = os.path.join(tmpdir, "vdeg.wav")

                            subprocess.run(["ffmpeg", "-y", "-i", input_path, "-ar", str(v_rate), v_ref], capture_output=True)
                            subprocess.run(["ffmpeg", "-y", "-i", deg_wav, "-ar", str(v_rate), v_deg], capture_output=True)

                            mos = run_visqol(v_ref, v_deg, cfg["mode"])

                    results["matrix"][key] = {
                        "mos": mos,
                        "size": aac_size,
                        "thresh": cfg["thresh"],
                        "scenario": name,
                        "filename": sample
                    }
                except:
                    pass

    speech_dir = os.path.join(EXTERNAL_DATA_DIR, "speech")
    if os.path.exists(speech_dir):
        samples = sorted([f for f in os.listdir(speech_dir) if f.endswith(".wav")])
        if samples:
            input_path = os.path.join(speech_dir, samples[0])
            output_path = os.path.join(OUTPUT_DIR, f"throughput_{precision}.aac")
            start_time = time.time()
            try:
                for _ in range(3):
                    subprocess.run([faac_path, "-o", output_path, input_path], env=env, check=True, capture_output=True)
                results["throughput"]["overall"] = (time.time() - start_time) / 3
            except:
                pass

    return results

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print("Usage: python3 tests/run_benchmark.py <faac_bin_path> <lib_path> <precision_name> <output_json> [--perceptual] [--coverage 100]")
        sys.exit(1)

    do_perc = "--perceptual" in sys.argv
    coverage = 100
    if "--coverage" in sys.argv:
        idx = sys.argv.index("--coverage")
        coverage = int(sys.argv[idx+1])

    data = run_benchmark(sys.argv[1], sys.argv[2], sys.argv[3], coverage=coverage, run_perceptual=do_perc)
    with open(sys.argv[4], "w") as f:
        json.dump(data, f, indent=2)
