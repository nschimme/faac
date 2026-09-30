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
REF_APPLE_DIR = LADDER_DIR / "ref/apple_he48k"

FAAC_BIN = "/home/jules/bin/faac_probe"
FAAD_BIN = "/tmp/faad-ladder-dump/build_faad/frontend/faad"
FDK_BIN = "/home/jules/bin/fdk_he"
SCORE_CLIP = str(BENCHMARK_DIR / "scripts/score_clip.py")
H_CONV = LADDER_DIR / "scripts/h/h_conv.py"

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

def pad_wav(src_wav, pad_samples, out_wav):
    data, sr = sf.read(src_wav)
    assert sr == 48000
    if pad_samples > 0:
        if data.ndim == 1:
            zeros = np.zeros(pad_samples, dtype=data.dtype)
        else:
            zeros = np.zeros((pad_samples, data.shape[1]), dtype=data.dtype)
        padded = np.concatenate([zeros, data], axis=0)
    else:
        padded = data
    sf.write(out_wav, padded, sr)

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

def dump_adts_with_faad(m4a_path, dump_txt, pcm_wav):
    env = dict(os.environ, FAAD_LADDER_DUMP="1", FAAD_DUMP=str(dump_txt))
    cmd = [FAAD_BIN, "-o", str(pcm_wav), str(m4a_path)]
    subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)

def calc_short_block_share(dump_txt):
    parsed = pd.parse(str(dump_txt))
    tot_ics = 0
    short_ics = 0
    for frame_idx, chs in parsed.items():
        for ch_idx, ics in chs.items():
            tot_ics += 1
            if ics.win_seq == 2:  # EIGHT_SHORT
                short_ics += 1
    return short_ics / tot_ics if tot_ics > 0 else 0.0

def run_step_b():
    print("=== Step B: Base Slopes & Window Injection Arms (W_fdk & W_apple) ===")
    clips = get_audio_clips()
    b_dir = WORK_DIR / "step_b"
    b_dir.mkdir(parents=True, exist_ok=True)

    a0_base_dir = WORK_DIR / "a0_base"
    a1_ref_dir = WORK_DIR / "a1_ref"

    base_scores_cache_file = WORK_DIR / "base_scores_cache.json"
    if base_scores_cache_file.exists():
        print("Loading cached base scores...")
        base_scores = json.loads(base_scores_cache_file.read_text())
    else:
        base_scores = {}
        print("Generating base encodes (F40, F48, F56)...")
        for clip_path in clips:
            stem = clip_path.stem
            f40_m4a = b_dir / f"{stem}_F40.m4a"
            f56_m4a = b_dir / f"{stem}_F56.m4a"
            env = dict(os.environ, **BASE_ENV)
            if not f40_m4a.exists():
                subprocess.run([FAAC_BIN, "--overwrite", "-b", "40", "-o", str(f40_m4a), str(clip_path)], env=env, check=True)
            if not f56_m4a.exists():
                subprocess.run([FAAC_BIN, "--overwrite", "-b", "56", "-o", str(f56_m4a), str(clip_path)], env=env, check=True)

        print("Preparing cropped wavs for base scoring...")
        score_tasks = []
        for clip_path in clips:
            stem = clip_path.stem
            orig_samples = len(sf.read(clip_path)[0])

            f48_m4a = a0_base_dir / f"{stem}_F48.m4a"
            f48_wav = b_dir / f"{stem}_F48_crop.wav"
            if not f48_wav.exists():
                decode_and_crop(f48_m4a, 0, orig_samples, f48_wav)

            f40_m4a = b_dir / f"{stem}_F40.m4a"
            f40_wav = b_dir / f"{stem}_F40_crop.wav"
            if not f40_wav.exists():
                decode_and_crop(f40_m4a, 0, orig_samples, f40_wav)

            f56_m4a = b_dir / f"{stem}_F56.m4a"
            f56_wav = b_dir / f"{stem}_F56_crop.wav"
            if not f56_wav.exists():
                decode_and_crop(f56_m4a, 0, orig_samples, f56_wav)

            score_tasks.append(((stem, "48"), clip_path, f48_wav))
            score_tasks.append(((stem, "40"), clip_path, f40_wav))
            score_tasks.append(((stem, "56"), clip_path, f56_wav))

        print("Scoring base encodes in parallel (4 workers)...")
        mos_map = {}
        with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
            futures = [executor.submit(score_clip_single, task) for task in score_tasks]
            for idx, fut in enumerate(concurrent.futures.as_completed(futures)):
                key, mos = fut.result()
                mos_map[key] = mos
                if (idx + 1) % 20 == 0 or idx + 1 == len(futures):
                    print(f"  Base scored {idx+1}/{len(futures)}...")

        for clip_path in clips:
            stem = clip_path.stem
            f48_m4a = a0_base_dir / f"{stem}_F48.m4a"
            f40_m4a = b_dir / f"{stem}_F40.m4a"
            f56_m4a = b_dir / f"{stem}_F56.m4a"

            mos_48 = mos_map[(stem, "48")]
            mos_40 = mos_map[(stem, "40")]
            mos_56 = mos_map[(stem, "56")]

            bytes_48 = f48_m4a.stat().st_size
            bytes_40 = f40_m4a.stat().st_size
            bytes_56 = f56_m4a.stat().st_size

            (b_dir / f"{stem}_F48_crop.wav").unlink(missing_ok=True)
            (b_dir / f"{stem}_F40_crop.wav").unlink(missing_ok=True)
            (b_dir / f"{stem}_F56_crop.wav").unlink(missing_ok=True)

            slope = (mos_56 - mos_40) / math.log2(bytes_56 / bytes_40)
            base_scores[stem] = {
                "mos_48": mos_48, "bytes_48": bytes_48,
                "mos_40": mos_40, "bytes_40": bytes_40,
                "mos_56": mos_56, "bytes_56": bytes_56,
                "slope": slope
            }
        base_scores_cache_file.write_text(json.dumps(base_scores, indent=2))
        print("Base scores computed and cached.")

    # Short block share of base F48
    base_short_shares = {}
    for clip_path in clips:
        stem = clip_path.stem
        f_dump = a0_base_dir / f"{stem}_F48.dump"
        base_short_shares[stem] = calc_short_block_share(f_dump)

    avg_base_short_share = np.mean(list(base_short_shares.values()))
    print(f"Base F48 Average Short-Block Share: {avg_base_short_share*100:.2f}%")

    # Arms: W_fdk and W_apple
    results_arms = {}

    for arm_name, ref_tag, pad_s, inj_offset in [("w_fdk", "fdk", 2015, "1"), ("w_apple", "apple", 96, "2")]:
        arm_cache_file = WORK_DIR / f"{arm_name}_cache.json"
        if arm_cache_file.exists():
            print(f"Loading cached {arm_name.upper()} results...")
            results_arms[arm_name] = json.loads(arm_cache_file.read_text())
            continue

        print(f"\nRunning Arm {arm_name.upper()}...")
        arm_res_dict = {}

        arm_score_tasks = []
        arm_info_list = []

        for clip_path in clips:
            stem = clip_path.stem
            orig_samples = len(sf.read(clip_path)[0])

            ref_ci = a1_ref_dir / f"{stem}_{ref_tag}48.ci"
            padded_wav = a1_ref_dir / f"{stem}_{ref_tag}_padded.wav"

            out_m4a = b_dir / f"{stem}_{arm_name}.m4a"
            out_dump = b_dir / f"{stem}_{arm_name}.dump"
            out_faad_wav = b_dir / f"{stem}_{arm_name}_faad.wav"
            out_crop_wav = b_dir / f"{stem}_{arm_name}_crop.wav"

            if not out_m4a.exists():
                env_inj = dict(
                    os.environ,
                    **BASE_ENV,
                    FAAC_CORE_INJECT=str(ref_ci),
                    FAAC_CORE_INJECT_FIELDS="win",
                    FAAC_CORE_INJECT_OFFSET=inj_offset,
                    FAAC_CORE_INJECT_LOOSE_SFB="1"
                )
                subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(out_m4a), str(padded_wav)],
                               env=env_inj, check=True)

            if not out_crop_wav.exists():
                decode_and_crop(out_m4a, pad_s, orig_samples, out_crop_wav)

            if not out_dump.exists():
                dump_adts_with_faad(out_m4a, out_dump, out_faad_wav)

            short_share = calc_short_block_share(out_dump)

            arm_score_tasks.append((stem, clip_path, out_crop_wav))
            arm_info_list.append((stem, out_m4a, out_crop_wav, short_share))

        print(f"Scoring {arm_name.upper()} in parallel (4 workers)...")
        arm_mos_map = {}
        with concurrent.futures.ProcessPoolExecutor(max_workers=4) as executor:
            futures = [executor.submit(score_clip_single, task) for task in arm_score_tasks]
            for idx, fut in enumerate(concurrent.futures.as_completed(futures)):
                key, mos = fut.result()
                arm_mos_map[key] = mos
                if (idx + 1) % 10 == 0 or idx + 1 == len(futures):
                    print(f"  {arm_name.upper()} scored {idx+1}/{len(futures)}...")

        for stem, out_m4a, out_crop_wav, short_share in arm_info_list:
            mos_arm = arm_mos_map[stem]
            bytes_arm = out_m4a.stat().st_size
            out_crop_wav.unlink(missing_ok=True)

            mos_48 = base_scores[stem]["mos_48"]
            bytes_48 = base_scores[stem]["bytes_48"]
            slope = base_scores[stem]["slope"]

            raw_delta = mos_arm - mos_48
            adj_delta = raw_delta - slope * math.log2(bytes_arm / bytes_48)
            bytes_ratio = bytes_arm / bytes_48

            arm_res_dict[stem] = {
                "raw_mos": mos_arm,
                "bytes": bytes_arm,
                "bytes_ratio": bytes_ratio,
                "raw_delta": raw_delta,
                "adj_delta": adj_delta,
                "short_share": short_share
            }

        # Summary stats for arm
        adj_mean = np.mean([v["adj_delta"] for v in arm_res_dict.values()])
        adj_median = np.median([v["adj_delta"] for v in arm_res_dict.values()])
        wins = sum(1 for v in arm_res_dict.values() if v["adj_delta"] > 0)
        losses = sum(1 for v in arm_res_dict.values() if v["adj_delta"] < 0)
        avg_bytes_ratio = np.mean([v["bytes_ratio"] for v in arm_res_dict.values()])
        avg_short_share = np.mean([v["short_share"] for v in arm_res_dict.values()])

        sorted_clips = sorted(arm_res_dict.items(), key=lambda x: x[1]["adj_delta"], reverse=True)
        top5_best = [(k, v["adj_delta"]) for k, v in sorted_clips[:5]]
        top5_worst = [(k, v["adj_delta"]) for k, v in sorted_clips[-5:]]

        # Core gap recovery
        core_gap_range = (0.068, 0.099) if ref_tag == "fdk" else (0.056, 0.075)
        gap_share_low = adj_mean / core_gap_range[1] * 100
        gap_share_high = adj_mean / core_gap_range[0] * 100

        summary = {
            "adj_mean": adj_mean,
            "adj_median": adj_median,
            "wins": wins,
            "losses": losses,
            "bytes_ratio_mean": avg_bytes_ratio,
            "short_share_mean": avg_short_share,
            "gap_share_range_pct": (gap_share_low, gap_share_high),
            "top5_best": top5_best,
            "top5_worst": top5_worst,
            "clips": arm_res_dict
        }
        results_arms[arm_name] = summary
        arm_cache_file.write_text(json.dumps(summary, indent=2))

        print(f"--- {arm_name.upper()} Results ---")
        print(f"Adj Mean:          {adj_mean:+.4f}")
        print(f"Adj Median:        {adj_median:+.4f}")
        print(f"W / L:             {wins} / {losses}")
        print(f"Bytes Ratio Mean:  {avg_bytes_ratio:.4f}")
        print(f"Short-Block Share: {avg_short_share*100:.2f}% (Base was {avg_base_short_share*100:.2f}%)")
        print(f"Core Gap Share:    {gap_share_low:.1f}% .. {gap_share_high:.1f}% (vs {core_gap_range[0]}..{core_gap_range[1]})")
        print("Top 5 Best:")
        for k, v in top5_best:
            print(f"  {k:50s} : {v:+.4f}")
        print("Top 5 Worst:")
        for k, v in top5_worst:
            print(f"  {k:50s} : {v:+.4f}")

    out_file = WORK_DIR / "step_b_results.json"
    out_file.write_text(json.dumps({"base": base_scores, "arms": results_arms}, indent=2))
    print(f"\nStep B complete. Results saved to {out_file}")

if __name__ == "__main__":
    run_step_b()
