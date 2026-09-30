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
FAAD_BIN = "/tmp/faad-ladder-dump/build_faad/frontend/faad"
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

def run_step_c():
    print("=== Step C: PNS Threshold & Band Restriction Sweep at HE 48k ===")
    clips = get_audio_clips()
    c_dir = WORK_DIR / "step_c"
    c_dir.mkdir(parents=True, exist_ok=True)

    a0_base_dir = WORK_DIR / "a0_base"
    base_scores_cache_file = WORK_DIR / "base_scores_cache.json"
    if not base_scores_cache_file.exists():
        raise RuntimeError("base_scores_cache.json missing! Run step_b.py first.")

    base_scores = json.loads(base_scores_cache_file.read_text())

    # PNS Sweep Configurations
    pns_arms = [
        ("pns_thresh_0.0", {"FAAC_PNS_THRESH": "0.0"}),
        ("pns_thresh_0.1", {"FAAC_PNS_THRESH": "0.1"}),
        ("pns_thresh_0.2", {"FAAC_PNS_THRESH": "0.2"}),
        ("pns_thresh_0.3", {"FAAC_PNS_THRESH": "0.3"}),
        ("pns_thresh_0.5", {"FAAC_PNS_THRESH": "0.5"}),
        ("pns_thresh_0.6", {"FAAC_PNS_THRESH": "0.6"}),
        ("pns_min_sb_4",   {"FAAC_PNS_MIN_SB": "4"}),
        ("pns_min_sb_8",   {"FAAC_PNS_MIN_SB": "8"}),
        ("pns_min_sb_12",  {"FAAC_PNS_MIN_SB": "12"}),
        ("pns_min_sb_16",  {"FAAC_PNS_MIN_SB": "16"}),
    ]

    all_arm_results = {}
    passing_arms = []

    for arm_key, env_overrides in pns_arms:
        arm_cache_file = c_dir / f"{arm_key}_cache.json"
        if arm_cache_file.exists():
            print(f"Loading cached {arm_key}...")
            summary = json.loads(arm_cache_file.read_text())
            all_arm_results[arm_key] = summary
            if summary.get("passes_rule"):
                passing_arms.append(arm_key)
            continue

        print(f"\nEvaluating Arm {arm_key} ({env_overrides})...")
        arm_res_dict = {}
        score_tasks = []
        task_info_list = []

        for clip_path in clips:
            stem = clip_path.stem
            orig_samples = len(sf.read(clip_path)[0])

            out_m4a = c_dir / f"{stem}_{arm_key}.m4a"
            out_crop_wav = c_dir / f"{stem}_{arm_key}_crop.wav"

            env_arm = dict(os.environ, **BASE_ENV, **env_overrides)
            if not out_m4a.exists():
                subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(out_m4a), str(clip_path)],
                               env=env_arm, check=True)

            if not out_crop_wav.exists():
                decode_and_crop(out_m4a, 0, orig_samples, out_crop_wav)

            score_tasks.append((stem, clip_path, out_crop_wav))
            task_info_list.append((stem, out_m4a, out_crop_wav))

        print(f"Scoring {arm_key} in parallel (4 workers)...")
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

            mos_48 = base_scores[stem]["mos_48"]
            bytes_48 = base_scores[stem]["bytes_48"]
            slope = base_scores[stem]["slope"]

            raw_delta = mos_arm - mos_48
            adj_delta = raw_delta - slope * math.log2(bytes_arm / bytes_48)
            bytes_ratio = bytes_arm / bytes_48

            arm_res_dict[stem] = {
                "raw_mos": float(mos_arm),
                "bytes": int(bytes_arm),
                "bytes_ratio": float(bytes_ratio),
                "raw_delta": float(raw_delta),
                "adj_delta": float(adj_delta)
            }

        # Summary stats for arm
        adj_mean = float(np.mean([v["adj_delta"] for v in arm_res_dict.values()]))
        adj_median = float(np.median([v["adj_delta"] for v in arm_res_dict.values()]))
        wins = int(sum(1 for v in arm_res_dict.values() if v["adj_delta"] > 0))
        losses = int(sum(1 for v in arm_res_dict.values() if v["adj_delta"] < 0))
        avg_bytes_ratio = float(np.mean([v["bytes_ratio"] for v in arm_res_dict.values()]))
        min_clip_delta = float(min(v["adj_delta"] for v in arm_res_dict.values()))

        sorted_clips = sorted(arm_res_dict.items(), key=lambda x: x[1]["adj_delta"], reverse=True)
        top5_best = [(k, v["adj_delta"]) for k, v in sorted_clips[:5]]
        top5_worst = [(k, v["adj_delta"]) for k, v in sorted_clips[-5:]]

        # Check Encoder-Knob Rule criteria
        passes_rule = bool(
            adj_mean >= 0.005 and
            wins > losses and
            min_clip_delta >= -0.05 and
            0.875 <= avg_bytes_ratio <= 1.125
        )

        summary = {
            "env": env_overrides,
            "adj_mean": adj_mean,
            "adj_median": adj_median,
            "wins": wins,
            "losses": losses,
            "bytes_ratio_mean": avg_bytes_ratio,
            "min_clip_delta": min_clip_delta,
            "passes_rule": passes_rule,
            "top5_best": top5_best,
            "top5_worst": top5_worst,
            "clips": arm_res_dict
        }
        all_arm_results[arm_key] = summary
        arm_cache_file.write_text(json.dumps(summary, indent=2))

        if passes_rule:
            passing_arms.append(arm_key)

        print(f"--- {arm_key} Results ---")
        print(f"Adj Mean:         {adj_mean:+.4f}")
        print(f"Adj Median:       {adj_median:+.4f}")
        print(f"W / L:            {wins} / {losses}")
        print(f"Bytes Ratio Mean: {avg_bytes_ratio:.4f}")
        print(f"Min Clip Delta:   {min_clip_delta:+.4f}")
        print(f"Passes Rule:      {passes_rule}")

    out_file = WORK_DIR / "step_c_results.json"
    out_file.write_text(json.dumps(all_arm_results, indent=2))
    print(f"\nStep C HE 48k Sweep complete. Saved to {out_file}")
    print(f"Passing arms at HE 48k: {passing_arms}")

if __name__ == "__main__":
    run_step_c()
