#!/usr/bin/env python3
import os
import sys
import pathlib
import subprocess
import hashlib
import json
import re

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

def get_audio_clips():
    return sorted(list(AUDIO_DIR.glob("*.wav")))

def run_control_c1():
    print("=== Control C1: Threshold Equivalence Check (FAAC_PNS_THRESH=0.4 vs Unset) ===")
    clips = get_audio_clips()
    c1_dir = WORK_DIR / "control_c1"
    c1_dir.mkdir(parents=True, exist_ok=True)

    diffs_48k = 0
    diffs_32k = 0

    for rate in ["48", "32"]:
        print(f"Checking {rate}k ABR...")
        for clip in clips:
            stem = clip.stem
            out_unset = c1_dir / f"{stem}_{rate}k_unset.m4a"
            out_04 = c1_dir / f"{stem}_{rate}k_04.m4a"

            # 1. Unset
            env_unset = dict(os.environ, **BASE_ENV)
            if "FAAC_PNS_THRESH" in env_unset:
                del env_unset["FAAC_PNS_THRESH"]
            subprocess.run([FAAC_BIN, "--overwrite", "-b", rate, "-o", str(out_unset), str(clip)],
                           env=env_unset, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            # 2. FAAC_PNS_THRESH=0.4
            env_04 = dict(os.environ, **BASE_ENV, FAAC_PNS_THRESH="0.4")
            subprocess.run([FAAC_BIN, "--overwrite", "-b", rate, "-o", str(out_04), str(clip)],
                           env=env_04, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

            # Compare PCM
            pcm_unset = c1_dir / "pcm_unset.raw"
            pcm_04 = c1_dir / "pcm_04.raw"
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(out_unset), "-f", "s16le", str(pcm_unset)], check=True)
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(out_04), "-f", "s16le", str(pcm_04)], check=True)

            h1 = hashlib.md5(pcm_unset.read_bytes()).hexdigest()
            h2 = hashlib.md5(pcm_04.read_bytes()).hexdigest()

            pcm_unset.unlink(missing_ok=True)
            pcm_04.unlink(missing_ok=True)

            if h1 != h2:
                print(f"  DIFF detected for {stem} at {rate}k!")
                if rate == "48": diffs_48k += 1
                else: diffs_32k += 1

    print(f"Control C1 Result: 48k diffs = {diffs_48k}/49, 32k diffs = {diffs_32k}/49")
    if diffs_48k != 0 or diffs_32k != 0:
        raise RuntimeError("Control C1 FAILED: 0.4 threshold did not produce 100% identical PCM to unset!")
    print("Control C1: PASS")
    return True

def run_control_c2():
    print("\n=== Control C2: Scorer Determinism Check ===")
    clips = get_audio_clips()[:5]
    c2_dir = WORK_DIR / "control_c2"
    c2_dir.mkdir(parents=True, exist_ok=True)

    scores1 = []
    scores2 = []

    for clip in clips:
        out_m4a = c2_dir / f"{clip.stem}_48k.m4a"
        env = dict(os.environ, **BASE_ENV)
        subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(out_m4a), str(clip)],
                       env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        # Score run 1
        p1 = subprocess.run([sys.executable, SCORE_CLIP, str(clip), str(out_m4a)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
        m1 = float(re.search(r'MOS: ([\d.]+)', p1.stdout).group(1))

        # Score run 2
        p2 = subprocess.run([sys.executable, SCORE_CLIP, str(clip), str(out_m4a)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
        m2 = float(re.search(r'MOS: ([\d.]+)', p2.stdout).group(1))

        scores1.append(m1)
        scores2.append(m2)
        print(f"  {clip.stem}: Run 1 = {m1:.6f}, Run 2 = {m2:.6f}")

    diffs = sum(1 for s1, s2 in zip(scores1, scores2) if s1 != s2)
    print(f"Control C2 Result: {diffs}/5 differing MOS scores")
    if diffs != 0:
        raise RuntimeError("Control C2 FAILED: Scorer was non-deterministic!")
    print("Control C2: PASS")
    return True

def measure_pns_share_for_arm(thresh_str, rate="48"):
    c3_dir = WORK_DIR / "control_c3"
    c3_dir.mkdir(parents=True, exist_ok=True)
    clips = get_audio_clips()

    long_total = 0
    long_pns = 0
    short_total = 0
    short_pns = 0

    env = dict(os.environ, **BASE_ENV)
    if thresh_str is not None:
        env["FAAC_PNS_THRESH"] = thresh_str
    elif "FAAC_PNS_THRESH" in env:
        del env["FAAC_PNS_THRESH"]

    for clip in clips:
        stem = clip.stem
        out_m4a = c3_dir / f"{stem}_{rate}k_t{thresh_str}.m4a"
        out_dump = c3_dir / f"{stem}_{rate}k_t{thresh_str}_dump.txt"

        subprocess.run([FAAC_BIN, "--overwrite", "-b", rate, "-o", str(out_m4a), str(clip)],
                       env=env, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        env_faad = dict(os.environ, FAAD_DUMP=str(out_dump), FAAD_LADDER_DUMP="1")
        subprocess.run([FAAD_BIN, "-o", str(c3_dir / "dummy.wav"), str(out_m4a)],
                       env=env_faad, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        frames = pd.parse(out_dump)
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

        out_m4a.unlink(missing_ok=True)
        out_dump.unlink(missing_ok=True)

    long_share = long_pns / max(1, long_total)
    short_share = short_pns / max(1, short_total)
    total_share = (long_pns + short_pns) / max(1, long_total + short_total)

    return {
        "thresh": thresh_str,
        "long_pns": long_pns,
        "long_total": long_total,
        "long_share": long_share,
        "short_pns": short_pns,
        "short_total": short_total,
        "short_share": short_share,
        "total_share": total_share
    }

def run_control_c3():
    print("\n=== Control C3: PNS Band Share Monotonicity Check across Thresholds ===")
    thresholds = ["0.2", "0.3", "0.35", "0.4", "0.45", "0.5"]
    pns_stats = {}

    for t in thresholds:
        print(f"Measuring PNS band share for FAAC_PNS_THRESH={t} at 48k...")
        stats = measure_pns_share_for_arm(t, rate="48")
        pns_stats[t] = stats
        print(f"  Thresh {t}: Long = {stats['long_share']*100:.2f}%, Short = {stats['short_share']*100:.2f}%, Total = {stats['total_share']*100:.2f}%")

    # Check monotonicity
    shares = [pns_stats[t]["total_share"] for t in thresholds]
    is_monotonic = all(x <= y for x, y in zip(shares, shares[1:]))
    print(f"Total PNS Shares: {[round(s*100, 2) for s in shares]} %")
    print(f"Is Monotonic with Threshold? {is_monotonic}")

    if not is_monotonic:
        raise RuntimeError("Control C3 FAILED: PNS band share was not monotonic with threshold!")

    out_file = WORK_DIR / "control_c3_results.json"
    out_file.write_text(json.dumps(pns_stats, indent=2))
    print("Control C3: PASS")
    return True

if __name__ == "__main__":
    run_control_c1()
    run_control_c2()
    run_control_c3()
