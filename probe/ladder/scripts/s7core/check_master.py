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

PLAIN_MASTER_ENV = {}

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

def run_master_check():
    print("=== Re-measuring Candidate pns_min_sb_4 on Plain Master ===")
    clips = get_audio_clips()
    m_dir = WORK_DIR / "plain_master_check"
    m_dir.mkdir(parents=True, exist_ok=True)

    print("Generating Plain Master HE 48k base encodes (M40, M48, M56)...")
    for clip_path in clips:
        stem = clip_path.stem
        m40_m4a = m_dir / f"{stem}_M40.m4a"
        m48_m4a = m_dir / f"{stem}_M48.m4a"
        m56_m4a = m_dir / f"{stem}_M56.m4a"
        if not m40_m4a.exists():
            subprocess.run([FAAC_BIN, "--overwrite", "-b", "40", "-o", str(m40_m4a), str(clip_path)], env=PLAIN_MASTER_ENV, check=True)
        if not m48_m4a.exists():
            subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(m48_m4a), str(clip_path)], env=PLAIN_MASTER_ENV, check=True)
        if not m56_m4a.exists():
            subprocess.run([FAAC_BIN, "--overwrite", "-b", "56", "-o", str(m56_m4a), str(clip_path)], env=PLAIN_MASTER_ENV, check=True)

    print("Cropping & scoring Plain Master 48k base...")
    score_tasks = []
    for clip_path in clips:
        stem = clip_path.stem
        orig_samples = len(sf.read(clip_path)[0])

        m40_m4a = m_dir / f"{stem}_M40.m4a"
        m48_m4a = m_dir / f"{stem}_M48.m4a"
        m56_m4a = m_dir / f"{stem}_M56.m4a"

        m40_wav = m_dir / f"{stem}_M40_crop.wav"
        m48_wav = m_dir / f"{stem}_M48_crop.wav"
        m56_wav = m_dir / f"{stem}_M56_crop.wav"

        if not m40_wav.exists(): decode_and_crop(m40_m4a, 0, orig_samples, m40_wav)
        if not m48_wav.exists(): decode_and_crop(m48_m4a, 0, orig_samples, m48_wav)
        if not m56_wav.exists(): decode_and_crop(m56_m4a, 0, orig_samples, m56_wav)

        score_tasks.append(((stem, "40"), clip_path, m40_wav))
        score_tasks.append(((stem, "48"), clip_path, m48_wav))
        score_tasks.append(((stem, "56"), clip_path, m56_wav))

    mos_map = {}
    with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
        futures = [executor.submit(score_clip_single, task) for task in score_tasks]
        for fut in concurrent.futures.as_completed(futures):
            key, mos = fut.result()
            mos_map[key] = mos

    master_base_48 = {}
    for clip_path in clips:
        stem = clip_path.stem
        m40_m4a = m_dir / f"{stem}_M40.m4a"
        m48_m4a = m_dir / f"{stem}_M48.m4a"
        m56_m4a = m_dir / f"{stem}_M56.m4a"

        mos_40 = mos_map[(stem, "40")]
        mos_48 = mos_map[(stem, "48")]
        mos_56 = mos_map[(stem, "56")]

        bytes_40 = m40_m4a.stat().st_size
        bytes_48 = m48_m4a.stat().st_size
        bytes_56 = m56_m4a.stat().st_size

        (m_dir / f"{stem}_M40_crop.wav").unlink(missing_ok=True)
        (m_dir / f"{stem}_M48_crop.wav").unlink(missing_ok=True)
        (m_dir / f"{stem}_M56_crop.wav").unlink(missing_ok=True)

        slope = (mos_56 - mos_40) / math.log2(bytes_56 / bytes_40)
        master_base_48[stem] = {
            "mos_48": mos_48, "bytes_48": bytes_48,
            "slope": slope
        }

    print("Evaluating pns_min_sb_4 on Plain Master HE 48k...")
    knob_env = {"FAAC_PNS_MIN_SB": "4"}
    arm_score_tasks = []
    arm_info_list = []

    for clip_path in clips:
        stem = clip_path.stem
        orig_samples = len(sf.read(clip_path)[0])

        out_m4a = m_dir / f"{stem}_M48_min_sb_4.m4a"
        out_crop_wav = m_dir / f"{stem}_M48_min_sb_4_crop.wav"

        if not out_m4a.exists():
            subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(out_m4a), str(clip_path)], env=knob_env, check=True)
        if not out_crop_wav.exists():
            decode_and_crop(out_m4a, 0, orig_samples, out_crop_wav)

        arm_score_tasks.append((stem, clip_path, out_crop_wav))
        arm_info_list.append((stem, out_m4a, out_crop_wav))

    mos_map_knob = {}
    with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
        futures = [executor.submit(score_clip_single, task) for task in arm_score_tasks]
        for fut in concurrent.futures.as_completed(futures):
            key, mos = fut.result()
            mos_map_knob[key] = mos

    master_arm_res = {}
    for stem, out_m4a, out_crop_wav in arm_info_list:
        mos_arm = mos_map_knob[stem]
        bytes_arm = out_m4a.stat().st_size
        out_crop_wav.unlink(missing_ok=True)

        mos_48 = master_base_48[stem]["mos_48"]
        bytes_48 = master_base_48[stem]["bytes_48"]
        slope = master_base_48[stem]["slope"]

        raw_delta = mos_arm - mos_48
        adj_delta = raw_delta - slope * math.log2(bytes_arm / bytes_48)
        bytes_ratio = bytes_arm / bytes_48

        master_arm_res[stem] = {
            "raw_mos": float(mos_arm),
            "bytes": int(bytes_arm),
            "bytes_ratio": float(bytes_ratio),
            "raw_delta": float(raw_delta),
            "adj_delta": float(adj_delta)
        }

    adj_mean = float(np.mean([v["adj_delta"] for v in master_arm_res.values()]))
    adj_median = float(np.median([v["adj_delta"] for v in master_arm_res.values()]))
    wins = int(sum(1 for v in master_arm_res.values() if v["adj_delta"] > 0))
    losses = int(sum(1 for v in master_arm_res.values() if v["adj_delta"] < 0))
    avg_bytes_ratio = float(np.mean([v["bytes_ratio"] for v in master_arm_res.values()]))
    min_clip_delta = float(min(v["adj_delta"] for v in master_arm_res.values()))

    passes_rule = bool(
        adj_mean >= 0.005 and
        wins > losses and
        min_clip_delta >= -0.05 and
        0.875 <= avg_bytes_ratio <= 1.125
    )

    print("\n--- Plain Master pns_min_sb_4 HE 48k Results ---")
    print(f"Adj Mean:         {adj_mean:+.4f}")
    print(f"Adj Median:       {adj_median:+.4f}")
    print(f"W / L:            {wins} / {losses}")
    print(f"Bytes Ratio Mean: {avg_bytes_ratio:.4f}")
    print(f"Min Clip Delta:   {min_clip_delta:+.4f}")
    print(f"Passes Rule:      {passes_rule}")

    out_file = WORK_DIR / "master_pns_min_sb_4_results.json"
    out_file.write_text(json.dumps({
        "adj_mean": adj_mean,
        "adj_median": adj_median,
        "wins": wins,
        "losses": losses,
        "bytes_ratio_mean": avg_bytes_ratio,
        "min_clip_delta": min_clip_delta,
        "passes_rule": passes_rule,
        "clips": master_arm_res
    }, indent=2))

if __name__ == "__main__":
    run_master_check()
