#!/usr/bin/env python3
import os
import sys
import pathlib
import subprocess
import re
import math
import numpy as np
import soundfile as sf
import json
import concurrent.futures

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[3]
LADDER_DIR = SCRIPT_DIR.parents[1]

BENCHMARK_DIR = pathlib.Path("/opt/faac-benchmark")
AUDIO_DIR = BENCHMARK_DIR / "data/external/audio"

FAAC_BIN = "/home/jules/bin/faac_probe"
SCORE_CLIP = str(BENCHMARK_DIR / "scripts/score_clip.py")

sys.path.insert(0, str(LADDER_DIR))
import parse_dump as pd

WORK_DIR = pathlib.Path("/tmp/s7core_work")
WORK_DIR.mkdir(parents=True, exist_ok=True)

BASE_ENV = {
    "FAAC_SF_SMOOTH": "0.6",
    "FAAC_BS_DROPRATIO": "12",
    "FAAC_SBR_FREQ_SCALE": "3",
}

def get_audio_clips():
    return sorted(list(AUDIO_DIR.glob("*.wav")))

def decode_and_crop(m4a_path, pad_samples, orig_samples, out_wav):
    raw_f32 = out_wav.with_suffix('.f32')
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(m4a_path), "-f", "f32le", "-ac", "2", str(raw_f32)], check=True)
    x = np.fromfile(raw_f32, dtype='<f4').reshape(-1, 2)
    cropped = x[pad_samples : pad_samples + orig_samples]
    sf.write(out_wav, cropped, 48000, subtype='FLOAT')
    raw_f32.unlink()

def score_clip_single(args):
    key, src_wav, deg_wav = args
    p = subprocess.run([sys.executable, SCORE_CLIP, str(src_wav), str(deg_wav)],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    m = re.search(r'MOS: ([\d.]+)', p.stdout)
    if not m:
        raise RuntimeError(f"Score failed for {deg_wav}: {p.stdout}")
    return key, float(m.group(1))

def run_he32k_check():
    print("=== Checking HE 32k for Candidate PNS Knobs ===")
    clips = get_audio_clips()
    c32_dir = WORK_DIR / "step_c_32k"
    c32_dir.mkdir(parents=True, exist_ok=True)

    base32_scores_file = WORK_DIR / "base32_scores_cache.json"
    if base32_scores_file.exists():
        print("Loading cached HE 32k base scores...")
        base32_scores = json.loads(base32_scores_file.read_text())
    else:
        base32_scores = {}
        print("Generating HE 32k base encodes (F28, F32, F40)...")
        for clip_path in clips:
            stem = clip_path.stem
            f28_m4a = c32_dir / f"{stem}_F28.m4a"
            f32_m4a = c32_dir / f"{stem}_F32.m4a"
            f40_m4a = c32_dir / f"{stem}_F40.m4a"
            env = dict(os.environ, **BASE_ENV)
            if not f28_m4a.exists():
                subprocess.run([FAAC_BIN, "--overwrite", "-b", "28", "-o", str(f28_m4a), str(clip_path)], env=env, check=True)
            if not f32_m4a.exists():
                subprocess.run([FAAC_BIN, "--overwrite", "-b", "32", "-o", str(f32_m4a), str(clip_path)], env=env, check=True)
            if not f40_m4a.exists():
                subprocess.run([FAAC_BIN, "--overwrite", "-b", "40", "-o", str(f40_m4a), str(clip_path)], env=env, check=True)

        print("Preparing cropped wavs for 32k base scoring...")
        score_tasks = []
        for clip_path in clips:
            stem = clip_path.stem
            orig_samples = len(sf.read(clip_path)[0])

            f28_m4a = c32_dir / f"{stem}_F28.m4a"
            f28_wav = c32_dir / f"{stem}_F28_crop.wav"
            if not f28_wav.exists():
                decode_and_crop(f28_m4a, 0, orig_samples, f28_wav)

            f32_m4a = c32_dir / f"{stem}_F32.m4a"
            f32_wav = c32_dir / f"{stem}_F32_crop.wav"
            if not f32_wav.exists():
                decode_and_crop(f32_m4a, 0, orig_samples, f32_wav)

            f40_m4a = c32_dir / f"{stem}_F40.m4a"
            f40_wav = c32_dir / f"{stem}_F40_crop.wav"
            if not f40_wav.exists():
                decode_and_crop(f40_m4a, 0, orig_samples, f40_wav)

            score_tasks.append(((stem, "28"), clip_path, f28_wav))
            score_tasks.append(((stem, "32"), clip_path, f32_wav))
            score_tasks.append(((stem, "40"), clip_path, f40_wav))

        print("Scoring HE 32k base encodes in parallel (4 workers)...")
        mos_map = {}
        with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
            futures = [executor.submit(score_clip_single, task) for task in score_tasks]
            for idx, fut in enumerate(concurrent.futures.as_completed(futures)):
                key, mos = fut.result()
                mos_map[key] = mos

        for clip_path in clips:
            stem = clip_path.stem
            f28_m4a = c32_dir / f"{stem}_F28.m4a"
            f32_m4a = c32_dir / f"{stem}_F32.m4a"
            f40_m4a = c32_dir / f"{stem}_F40.m4a"

            mos_28 = mos_map[(stem, "28")]
            mos_32 = mos_map[(stem, "32")]
            mos_40 = mos_map[(stem, "40")]

            bytes_28 = f28_m4a.stat().st_size
            bytes_32 = f32_m4a.stat().st_size
            bytes_40 = f40_m4a.stat().st_size

            (c32_dir / f"{stem}_F28_crop.wav").unlink(missing_ok=True)
            (c32_dir / f"{stem}_F32_crop.wav").unlink(missing_ok=True)
            (c32_dir / f"{stem}_F40_crop.wav").unlink(missing_ok=True)

            slope = (mos_40 - mos_28) / math.log2(bytes_40 / bytes_28)
            base32_scores[stem] = {
                "mos_32": mos_32, "bytes_32": bytes_32,
                "mos_28": mos_28, "bytes_28": bytes_28,
                "mos_40": mos_40, "bytes_40": bytes_40,
                "slope": slope
            }
        base32_scores_file.write_text(json.dumps(base32_scores, indent=2))

    # Evaluate Candidate Arms at HE 32k
    candidate_arms = [
        ("pns_thresh_0.3", {"FAAC_PNS_THRESH": "0.3"}),
        ("pns_min_sb_4",   {"FAAC_PNS_MIN_SB": "4"}),
    ]

    res_32k = {}

    for arm_key, env_overrides in candidate_arms:
        print(f"\nEvaluating {arm_key} at HE 32k...")
        arm_res_dict = {}
        score_tasks = []
        task_info_list = []

        for clip_path in clips:
            stem = clip_path.stem
            orig_samples = len(sf.read(clip_path)[0])

            out_m4a = c32_dir / f"{stem}_{arm_key}_32k.m4a"
            out_crop_wav = c32_dir / f"{stem}_{arm_key}_32k_crop.wav"

            env_arm = dict(os.environ, **BASE_ENV, **env_overrides)
            if not out_m4a.exists():
                subprocess.run([FAAC_BIN, "--overwrite", "-b", "32", "-o", str(out_m4a), str(clip_path)],
                               env=env_arm, check=True)

            if not out_crop_wav.exists():
                decode_and_crop(out_m4a, 0, orig_samples, out_crop_wav)

            score_tasks.append((stem, clip_path, out_crop_wav))
            task_info_list.append((stem, out_m4a, out_crop_wav))

        print(f"Scoring {arm_key} 32k in parallel...")
        mos_map = {}
        with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
            futures = [executor.submit(score_clip_single, task) for task in score_tasks]
            for idx, fut in enumerate(concurrent.futures.as_completed(futures)):
                key, mos = fut.result()
                mos_map[key] = mos

        for stem, out_m4a, out_crop_wav in task_info_list:
            mos_arm = mos_map[stem]
            bytes_arm = out_m4a.stat().st_size
            out_crop_wav.unlink(missing_ok=True)

            mos_32 = base32_scores[stem]["mos_32"]
            bytes_32 = base32_scores[stem]["bytes_32"]
            slope = base32_scores[stem]["slope"]

            raw_delta = mos_arm - mos_32
            adj_delta = raw_delta - slope * math.log2(bytes_arm / bytes_32)
            bytes_ratio = bytes_arm / bytes_32

            arm_res_dict[stem] = {
                "raw_mos": float(mos_arm),
                "bytes": int(bytes_arm),
                "bytes_ratio": float(bytes_ratio),
                "raw_delta": float(raw_delta),
                "adj_delta": float(adj_delta)
            }

        adj_mean = float(np.mean([v["adj_delta"] for v in arm_res_dict.values()]))
        adj_median = float(np.median([v["adj_delta"] for v in arm_res_dict.values()]))
        wins = int(sum(1 for v in arm_res_dict.values() if v["adj_delta"] > 0))
        losses = int(sum(1 for v in arm_res_dict.values() if v["adj_delta"] < 0))
        avg_bytes_ratio = float(np.mean([v["bytes_ratio"] for v in arm_res_dict.values()]))
        min_clip_delta = float(min(v["adj_delta"] for v in arm_res_dict.values()))

        not_worse_at_32k = bool(adj_mean >= 0.0)

        summary_32k = {
            "adj_mean": adj_mean,
            "adj_median": adj_median,
            "wins": wins,
            "losses": losses,
            "bytes_ratio_mean": avg_bytes_ratio,
            "min_clip_delta": min_clip_delta,
            "not_worse_at_32k": not_worse_at_32k,
            "clips": arm_res_dict
        }
        res_32k[arm_key] = summary_32k

        print(f"--- {arm_key} HE 32k Results ---")
        print(f"Adj Mean:         {adj_mean:+.4f}")
        print(f"Adj Median:       {adj_median:+.4f}")
        print(f"W / L:            {wins} / {losses}")
        print(f"Bytes Ratio Mean: {avg_bytes_ratio:.4f}")
        print(f"Min Clip Delta:   {min_clip_delta:+.4f}")
        print(f"Not Worse at 32k: {not_worse_at_32k}")

    out_file = WORK_DIR / "he32k_check_results.json"
    out_file.write_text(json.dumps(res_32k, indent=2))
    print(f"\nHE 32k Check complete. Saved to {out_file}")

if __name__ == "__main__":
    run_he32k_check()
