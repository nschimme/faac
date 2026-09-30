#!/usr/bin/env python3
import os
import sys
import pathlib
import subprocess
import re
import math
import argparse
import numpy as np
import soundfile as sf
import json
import concurrent.futures

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
LADDER_DIR = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(LADDER_DIR))
import parse_dump as pd

BENCHMARK_DIR = pathlib.Path("/opt/faac-benchmark")
AUDIO_DIR = BENCHMARK_DIR / "data/external/audio"

FAAC_BIN = "/home/jules/bin/faac_probe"
FAAD_BIN = "/tmp/faad-ladder-dump/build_faad/frontend/faad"
SCORE_CLIP = str(BENCHMARK_DIR / "scripts/score_clip.py")

BASE_ENV = {
    "FAAC_SF_SMOOTH": "0.6",
    "FAAC_BS_DROPRATIO": "12",
    "FAAC_SBR_FREQ_SCALE": "3",
}

WORK_DIR = pathlib.Path("/tmp/s8p_work")
WORK_DIR.mkdir(parents=True, exist_ok=True)
RESULTS_DIR = LADDER_DIR / "results/s8p"
RESULTS_DIR.mkdir(parents=True, exist_ok=True)

def get_audio_clips():
    return sorted(list(AUDIO_DIR.glob("*.wav")))

def decode_and_crop(m4a_path, pad_samples, orig_samples, out_wav):
    raw_f32 = out_wav.with_suffix('.f32')
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(m4a_path), "-f", "f32le", "-ac", "2", str(raw_f32)], check=True)
    x = np.fromfile(raw_f32, dtype='<f4').reshape(-1, 2)
    cropped = x[pad_samples : pad_samples + orig_samples]
    sf.write(out_wav, cropped, 48000, subtype='FLOAT')
    raw_f32.unlink(missing_ok=True)

def score_clip_single(args):
    key, src_wav, deg_wav = args
    p = subprocess.run([sys.executable, SCORE_CLIP, str(src_wav), str(deg_wav)],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    m = re.search(r'MOS: ([\d.]+)', p.stdout)
    if not m:
        raise RuntimeError(f"Score failed for {deg_wav}: {p.stdout}")
    return key, float(m.group(1))

def measure_pns_single(m4a_path):
    dump_path = m4a_path.with_suffix('.txt')
    env_faad = dict(os.environ, FAAD_DUMP=str(dump_path), FAAD_LADDER_DUMP="1")
    dummy_wav = m4a_path.with_suffix('.dummy.wav')
    subprocess.run([FAAD_BIN, "-o", str(dummy_wav), str(m4a_path)],
                   env=env_faad, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dummy_wav.unlink(missing_ok=True)

    frames = pd.parse(dump_path)
    dump_path.unlink(missing_ok=True)

    long_total = 0; long_pns = 0
    short_total = 0; short_pns = 0

    for fr, chs in frames.items():
        for ch, ics in chs.items():
            if not ics.present: continue
            is_short = (ics.win_seq == 2)
            cb_list = ics.band_cb[:ics.num_bands]
            for cb in cb_list:
                if is_short:
                    short_total += 1
                    if cb == 13: short_pns += 1
                else:
                    long_total += 1
                    if cb == 13: long_pns += 1

    return m4a_path.stem, {
        "long_pns": long_pns, "long_total": long_total, "long_share": long_pns / max(1, long_total),
        "short_pns": short_pns, "short_total": short_total, "short_share": short_pns / max(1, short_total),
        "total_share": (long_pns + short_pns) / max(1, long_total + short_total)
    }

def encode_and_score_rung(rate, lo_anchor, hi_anchor, arms_list):
    clips = get_audio_clips()
    rung_dir = WORK_DIR / f"rung_{rate}k"
    rung_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n=======================================================")
    print(f"--- Encoding and Scoring Rung {rate}k (Anchors: {lo_anchor}k / {hi_anchor}k) ---")
    print(f"=======================================================")

    # 1. Slope Anchors (Base F, threshold unset)
    print("Encoding slope anchors...")
    for clip in clips:
        stem = clip.stem
        orig_samples = len(sf.read(clip)[0])

        lo_m4a = rung_dir / f"{stem}_slope_{lo_anchor}k.m4a"
        hi_m4a = rung_dir / f"{stem}_slope_{hi_anchor}k.m4a"
        lo_wav = rung_dir / f"{stem}_slope_{lo_anchor}k_crop.wav"
        hi_wav = rung_dir / f"{stem}_slope_{hi_anchor}k_crop.wav"

        env_base = dict(os.environ, **BASE_ENV)
        if "FAAC_PNS_THRESH" in env_base: del env_base["FAAC_PNS_THRESH"]

        if not lo_m4a.exists():
            subprocess.run([FAAC_BIN, "--overwrite", "-b", lo_anchor, "-o", str(lo_m4a), str(clip)],
                           env=env_base, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not hi_m4a.exists():
            subprocess.run([FAAC_BIN, "--overwrite", "-b", hi_anchor, "-o", str(hi_m4a), str(clip)],
                           env=env_base, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        if not lo_wav.exists(): decode_and_crop(lo_m4a, 0, orig_samples, lo_wav)
        if not hi_wav.exists(): decode_and_crop(hi_m4a, 0, orig_samples, hi_wav)

    # Score anchors in parallel
    print("Scoring slope anchors...")
    lo_tasks = [(clip.stem, clip, rung_dir / f"{clip.stem}_slope_{lo_anchor}k_crop.wav") for clip in clips]
    hi_tasks = [(clip.stem, clip, rung_dir / f"{clip.stem}_slope_{hi_anchor}k_crop.wav") for clip in clips]

    lo_mos = {}; hi_mos = {}
    with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
        for fut in concurrent.futures.as_completed([executor.submit(score_clip_single, t) for t in lo_tasks]):
            k, m = fut.result(); lo_mos[k] = m
        for fut in concurrent.futures.as_completed([executor.submit(score_clip_single, t) for t in hi_tasks]):
            k, m = fut.result(); hi_mos[k] = m

    slopes = {}

    for clip in clips:
        stem = clip.stem
        lo_b = (rung_dir / f"{stem}_slope_{lo_anchor}k.m4a").stat().st_size
        hi_b = (rung_dir / f"{stem}_slope_{hi_anchor}k.m4a").stat().st_size
        m_lo = lo_mos[stem]
        m_hi = hi_mos[stem]

        slope = (m_hi - m_lo) / math.log2(hi_b / lo_b)
        slopes[stem] = slope

        # Cleanup anchor wavs
        (rung_dir / f"{stem}_slope_{lo_anchor}k_crop.wav").unlink(missing_ok=True)
        (rung_dir / f"{stem}_slope_{hi_anchor}k_crop.wav").unlink(missing_ok=True)

    # Reorder arms_list so 0.4_baseF is processed FIRST
    ordered_arms = []
    base_arm_tuple = None
    for item in arms_list:
        if item[0] == "0.4_baseF":
            base_arm_tuple = item
        else:
            ordered_arms.append(item)
    if base_arm_tuple:
        ordered_arms.insert(0, base_arm_tuple)

    base_f_bytes = {}
    base_f_mos = {}
    rung_arm_results = {}

    for arm_name, t_val in ordered_arms:
        print(f"\nProcessing Arm {arm_name} (FAAC_PNS_THRESH={t_val})...")
        arm_dir = rung_dir / arm_name
        arm_dir.mkdir(parents=True, exist_ok=True)

        env_arm = dict(os.environ, **BASE_ENV)
        if t_val is not None:
            env_arm["FAAC_PNS_THRESH"] = str(t_val)
        elif "FAAC_PNS_THRESH" in env_arm:
            del env_arm["FAAC_PNS_THRESH"]

        score_tasks = []
        pns_tasks = []

        for clip in clips:
            stem = clip.stem
            orig_samples = len(sf.read(clip)[0])
            out_m4a = arm_dir / f"{stem}.m4a"
            out_wav = arm_dir / f"{stem}_crop.wav"

            if not out_m4a.exists():
                subprocess.run([FAAC_BIN, "--overwrite", "-b", rate, "-o", str(out_m4a), str(clip)],
                               env=env_arm, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            if not out_wav.exists():
                decode_and_crop(out_m4a, 0, orig_samples, out_wav)

            score_tasks.append((stem, clip, out_wav))
            pns_tasks.append(out_m4a)

        # Score arm clips in parallel
        mos_map = {}
        with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
            for fut in concurrent.futures.as_completed([executor.submit(score_clip_single, t) for t in score_tasks]):
                k, m = fut.result(); mos_map[k] = m

        # Measure PNS share in parallel
        pns_map = {}
        with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
            for fut in concurrent.futures.as_completed([executor.submit(measure_pns_single, p) for p in pns_tasks]):
                k, info = fut.result(); pns_map[k] = info

        pns_long_tot = 0; pns_long_sel = 0
        pns_short_tot = 0; pns_short_sel = 0

        arm_clip_data = {}

        for clip in clips:
            stem = clip.stem
            out_m4a = arm_dir / f"{stem}.m4a"
            out_wav = arm_dir / f"{stem}_crop.wav"

            mos_val = mos_map[stem]
            b_val = out_m4a.stat().st_size

            if arm_name == "0.4_baseF":
                base_f_bytes[stem] = b_val
                base_f_mos[stem] = mos_val

            # PNS stats
            pns_info = pns_map[stem]
            pns_long_tot += pns_info["long_total"]
            pns_long_sel += pns_info["long_pns"]
            pns_short_tot += pns_info["short_total"]
            pns_short_sel += pns_info["short_pns"]

            out_wav.unlink(missing_ok=True)

            arm_clip_data[stem] = {
                "raw_mos": mos_val,
                "bytes": b_val,
                "pns_long_share": pns_info["long_share"],
                "pns_short_share": pns_info["short_share"],
                "pns_total_share": pns_info["total_share"]
            }

        # Calculate adjusted deltas relative to Base F (0.4_baseF)
        for stem, cdata in arm_clip_data.items():
            f_b = base_f_bytes[stem]
            f_m = base_f_mos[stem]
            slp = slopes[stem]

            raw_d = cdata["raw_mos"] - f_m
            adj_d = raw_d - slp * math.log2(cdata["bytes"] / f_b)
            cdata["raw_delta"] = float(raw_d)
            cdata["adj_delta"] = float(adj_d)
            cdata["bytes_ratio"] = float(cdata["bytes"] / f_b)

        adj_mean = float(np.mean([v["adj_delta"] for v in arm_clip_data.values()]))
        adj_median = float(np.median([v["adj_delta"] for v in arm_clip_data.values()]))
        wins = int(sum(1 for v in arm_clip_data.values() if v["adj_delta"] > 0.0005))
        losses = int(sum(1 for v in arm_clip_data.values() if v["adj_delta"] < -0.0005))
        ties = int(len(arm_clip_data) - wins - losses)
        bytes_ratio_mean = float(np.mean([v["bytes_ratio"] for v in arm_clip_data.values()]))
        min_clip_delta = float(min(v["adj_delta"] for v in arm_clip_data.values()))

        pns_long_share_mean = float(pns_long_sel / max(1, pns_long_tot))
        pns_short_share_mean = float(pns_short_sel / max(1, pns_short_tot))
        pns_total_share_mean = float((pns_long_sel + pns_short_sel) / max(1, pns_long_tot + pns_short_tot))

        rung_arm_results[arm_name] = {
            "thresh": t_val,
            "adj_mean": adj_mean,
            "adj_median": adj_median,
            "wins": wins,
            "losses": losses,
            "ties": ties,
            "bytes_ratio_mean": bytes_ratio_mean,
            "min_clip_delta": min_clip_delta,
            "pns_long_share": pns_long_share_mean,
            "pns_short_share": pns_short_share_mean,
            "pns_total_share": pns_total_share_mean,
            "clips": arm_clip_data
        }

        print(f"  Arm {arm_name} Summary:")
        print(f"    Adj Mean Δ:   {adj_mean:+.5f}")
        print(f"    Adj Median Δ: {adj_median:+.5f}")
        print(f"    W / L / T:    {wins} / {losses} / {ties}")
        print(f"    Bytes Ratio:  {bytes_ratio_mean:.5f}")
        print(f"    Min Clip Δ:   {min_clip_delta:+.5f}")
        print(f"    PNS Share:    Long = {pns_long_share_mean*100:.2f}%, Short = {pns_short_share_mean*100:.2f}%, Total = {pns_total_share_mean*100:.2f}%")

    return rung_arm_results

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rung", choices=["48", "32"], help="Run specific rung")
    args = parser.parse_args()

    arms_initial = [
        ("0.4_baseF", 0.4),
        ("0.2", 0.2),
        ("0.3", 0.3),
        ("0.35", 0.35),
        ("0.45", 0.45),
        ("0.5", 0.5),
    ]

    if args.rung == "48" or args.rung is None:
        he48_results = encode_and_score_rung("48", "40", "56", arms_initial)
        (RESULTS_DIR / "he48_sweep.json").write_text(json.dumps(he48_results, indent=2))
        print(f"\nSaved HE 48k results under {RESULTS_DIR / 'he48_sweep.json'}")

    if args.rung == "32" or args.rung is None:
        he32_results = encode_and_score_rung("32", "28", "40", arms_initial)
        (RESULTS_DIR / "he32_sweep.json").write_text(json.dumps(he32_results, indent=2))
        print(f"\nSaved HE 32k results under {RESULTS_DIR / 'he32_sweep.json'}")

if __name__ == "__main__":
    main()
