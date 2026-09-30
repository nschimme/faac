#!/usr/bin/env python3
import os
import sys
import pathlib
import subprocess
import json
import numpy as np
import soundfile as sf
from concurrent.futures import ProcessPoolExecutor

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
LADDER_DIR = SCRIPT_DIR.parents[1]
REPO_ROOT = SCRIPT_DIR.parents[2]

BENCHMARK_DIR = pathlib.Path("/opt/faac-benchmark")
AUDIO_DIR = BENCHMARK_DIR / "data/external/audio"
REF_APPLE_DIR = LADDER_DIR / "ref/apple_he48k"

FAAC_BIN = os.environ.get("FAAC_BIN", str(REPO_ROOT / "build_ladder/frontend/faac"))
if not pathlib.Path(FAAC_BIN).exists():
    FAAC_BIN = "/home/jules/bin/faac_probe"

FAAD_BIN = os.environ.get("FAAD_BIN", "/tmp/faad-ladder-dump/build_faad/frontend/faad")

FDK_BIN = os.environ.get("FDK_BIN", "/home/jules/bin/fdk_he")
H_CONV = LADDER_DIR / "scripts/h/h_conv.py"

sys.path.insert(0, str(LADDER_DIR))
import parse_dump as pd

WORK_DIR = pathlib.Path("/tmp/s8w_work")
WORK_DIR.mkdir(parents=True, exist_ok=True)

BASE_ENV = {
    "FAAC_SF_SMOOTH": "0.6",
    "FAAC_BS_DROPRATIO": "12",
    "FAAC_SBR_FREQ_SCALE": "3",
}

PARSED_DUMP_CACHE = {}

def get_parsed_dump(path):
    spath = str(path)
    if spath not in PARSED_DUMP_CACHE:
        PARSED_DUMP_CACHE[spath] = pd.parse(spath)
    return PARSED_DUMP_CACHE[spath]

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

def decode_to_pcm(m4a_path, pcm_wav):
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", str(m4a_path), "-f", "wav", str(pcm_wav)]
    subprocess.run(cmd, check=True)

def dump_adts_with_faad(m4a_path, dump_txt, pcm_wav):
    env = dict(os.environ, FAAD_LADDER_DUMP="1", FAAD_DUMP=str(dump_txt))
    cmd = [FAAD_BIN, "-o", str(pcm_wav), str(m4a_path)]
    subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)

def run_step_a0():
    print("=== Step A0: Base F encodes & Self-Injection Controls ===")
    clips = get_audio_clips()
    base_dir = WORK_DIR / "a0_base"
    base_dir.mkdir(parents=True, exist_ok=True)

    a0_results = {}
    full_ident_count = 0
    win_ident_count = 0

    for clip_path in clips:
        stem = clip_path.stem
        f_m4a = base_dir / f"{stem}_F48.m4a"
        f_dump = base_dir / f"{stem}_F48.dump"
        f_ci = base_dir / f"{stem}_F48.ci"
        f_wav = base_dir / f"{stem}_F48.wav"

        # 1. Base encode F
        if not f_m4a.exists():
            env = dict(os.environ, **BASE_ENV)
            subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(f_m4a), str(clip_path)], env=env, check=True)

        # 2. Dump F using FAAD3
        if not f_dump.exists():
            dump_adts_with_faad(f_m4a, f_dump, f_wav)

        # 3. Convert dump to .ci
        if not f_ci.exists():
            subprocess.run([sys.executable, str(H_CONV), str(f_dump), str(f_ci)], check=True)

        # 4a. Self-inject FULL (all fields: win,cls,sf,ms,tns)
        a0_full_m4a = base_dir / f"{stem}_A0_full.m4a"
        env_full = dict(
            os.environ,
            **BASE_ENV,
            FAAC_CORE_INJECT=str(f_ci),
            FAAC_CORE_INJECT_FIELDS="win,cls,sf,ms,tns",
            FAAC_CORE_INJECT_OFFSET="1",
            FAAC_CORE_INJECT_LOOSE_SFB="1",
            FAAC_CORE_INJECT_DEBUG="1"
        )
        subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(a0_full_m4a), str(clip_path)], env=env_full, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)

        # 4b. Self-inject WIN ONLY
        a0_win_m4a = base_dir / f"{stem}_A0_win.m4a"
        env_win = dict(
            os.environ,
            **BASE_ENV,
            FAAC_CORE_INJECT=str(f_ci),
            FAAC_CORE_INJECT_FIELDS="win",
            FAAC_CORE_INJECT_OFFSET="1",
            FAAC_CORE_INJECT_LOOSE_SFB="1",
            FAAC_CORE_INJECT_DEBUG="1"
        )
        subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(a0_win_m4a), str(clip_path)], env=env_win, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)

        # Decode F, A0_full, A0_win to PCM and compare
        f_pcm_path = base_dir / f"{stem}_F48_dec.wav"
        a0_full_pcm_path = base_dir / f"{stem}_A0_full_dec.wav"
        a0_win_pcm_path = base_dir / f"{stem}_A0_win_dec.wav"

        if not f_pcm_path.exists():
            decode_to_pcm(f_m4a, f_pcm_path)
        decode_to_pcm(a0_full_m4a, a0_full_pcm_path)
        decode_to_pcm(a0_win_m4a, a0_win_pcm_path)

        d_f, _ = sf.read(f_pcm_path)
        d_full, _ = sf.read(a0_full_pcm_path)
        d_win, _ = sf.read(a0_win_pcm_path)

        is_full_ident = np.array_equal(d_f, d_full)
        is_win_ident = np.array_equal(d_f, d_win)

        if is_full_ident:
            full_ident_count += 1

        if is_win_ident:
            win_ident_count += 1
        else:
            print(f"Clip {stem} WIN self-inject NOT identical, max diff: {np.max(np.abs(d_f - d_win))}")

        a0_results[stem] = {
            "full_ident": bool(is_full_ident),
            "win_ident": bool(is_win_ident)
        }

    print(f"Step A0 Result: FULL self-injection PCM identical on {full_ident_count}/{len(clips)} clips.")
    print(f"Step A0 Result: WIN-only self-injection PCM identical on {win_ident_count}/{len(clips)} clips.")
    assert win_ident_count == len(clips), f"A0 WIN self-injection failed ({win_ident_count}/{len(clips)})"

    return a0_results

def process_clip_inj(args):
    clip_path, ref_name, pad, offset, pad_dir, inj_dir, ref_dir = args
    stem = clip_path.stem
    p_wav = pad_dir / clip_path.name
    ref_ci = ref_dir / f"{stem}_{ref_name}48.ci"
    ref_dump = ref_dir / f"{stem}_{ref_name}48.dump"

    inj_m4a = inj_dir / f"{stem}.m4a"
    inj_dump = inj_dir / f"{stem}.dump"
    inj_wav = inj_dir / f"{stem}.wav"

    if not inj_dump.exists():
        env_inj = dict(
            os.environ,
            **BASE_ENV,
            FAAC_CORE_INJECT=str(ref_ci),
            FAAC_CORE_INJECT_FIELDS="win",
            FAAC_CORE_INJECT_OFFSET=str(offset),
            FAAC_CORE_INJECT_LOOSE_SFB="1"
        )
        subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(inj_m4a), str(p_wav)], env=env_inj, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        dump_adts_with_faad(inj_m4a, inj_dump, inj_wav)

    return (str(inj_dump), str(ref_dump), offset)

def compare_dump_pair(args):
    inj_dump_path, ref_dump_path, off = args
    faac_parsed = pd.parse(inj_dump_path)
    ref_parsed = pd.parse(ref_dump_path)

    total_windows = 0
    matched_windows = 0
    total_short_frames = 0
    matched_short_frames = 0

    for frame_idx, chs in faac_parsed.items():
        ref_frame_idx = frame_idx + off
        ref_chs = ref_parsed.get(ref_frame_idx, {})
        for ch_idx, ics in chs.items():
            ref_ics = ref_chs.get(ch_idx)
            if ref_ics is None:
                continue
            total_windows += 1
            if ics.win_seq == ref_ics.win_seq:
                matched_windows += 1

            is_faac_short = (ics.win_seq == 2)
            is_ref_short = (ref_ics.win_seq == 2)
            if is_ref_short:
                total_short_frames += 1
                if is_faac_short:
                    matched_short_frames += 1

    return (total_windows, matched_windows, total_short_frames, matched_short_frames)

def run_step_a1():
    print("\n=== Step A1: Alignment Sweep & Window Match Verification ===")
    clips = get_audio_clips()
    ref_dir = WORK_DIR / "a1_ref"
    ref_dir.mkdir(parents=True, exist_ok=True)

    # Prepare reference dumps and C-records
    print("Preparing FDK & Apple references...")
    for clip_path in clips:
        stem = clip_path.stem

        # FDK
        fdk_m4a = ref_dir / f"{stem}_fdk48.m4a"
        fdk_dump = ref_dir / f"{stem}_fdk48.dump"
        fdk_ci = ref_dir / f"{stem}_fdk48.ci"
        fdk_wav = ref_dir / f"{stem}_fdk48.wav"

        if not fdk_m4a.exists():
            subprocess.run([FDK_BIN, "-b", "48000", "-o", str(fdk_m4a), str(clip_path)], check=True)
        if not fdk_dump.exists():
            dump_adts_with_faad(fdk_m4a, fdk_dump, fdk_wav)
        if not fdk_ci.exists():
            subprocess.run([sys.executable, str(H_CONV), str(fdk_dump), str(fdk_ci)], check=True)

        # Apple
        apple_m4a = REF_APPLE_DIR / f"{stem}.m4a"
        apple_dump = ref_dir / f"{stem}_apple48.dump"
        apple_ci = ref_dir / f"{stem}_apple48.ci"
        apple_wav = ref_dir / f"{stem}_apple48.wav"

        if not apple_dump.exists():
            dump_adts_with_faad(apple_m4a, apple_dump, apple_wav)
        if not apple_ci.exists():
            subprocess.run([sys.executable, str(H_CONV), str(apple_dump), str(apple_ci)], check=True)

    # Sweep parameters: focused sweep around lag difference
    # fdk lag diff = 5057 - 3042 = 2015
    # Apple lag diff = 5186 - 3042 = 2144
    fdk_pads = [0, 961, 2015, 4063]
    apple_pads = [0, 96, 1024, 2144, 4192]
    offsets = [0, 1, 2, 3]

    def eval_sweep(ref_name, pads):
        sweep_results = []
        best_match = -1
        best_combo = None

        print(f"\n--- Sweeping alignment for {ref_name.upper()} ---")
        for pad in pads:
            pad_dir = ref_dir / f"padded_{ref_name}_{pad}"
            pad_dir.mkdir(parents=True, exist_ok=True)
            for clip_path in clips:
                p_wav = pad_dir / clip_path.name
                if not p_wav.exists():
                    pad_wav(clip_path, pad, p_wav)

            for offset in offsets:
                inj_dir = ref_dir / f"inj_{ref_name}_p{pad}_o{offset}"
                inj_dir.mkdir(parents=True, exist_ok=True)

                tasks = [(clip_path, ref_name, pad, offset, pad_dir, inj_dir, ref_dir) for clip_path in clips]
                with ProcessPoolExecutor(max_workers=4) as executor:
                    dump_pairs = list(executor.map(process_clip_inj, tasks))

                # Parse and compare dump pairs in separate processes
                total_windows = 0
                matched_windows = 0
                total_short_frames = 0
                matched_short_frames = 0

                with ProcessPoolExecutor(max_workers=4) as executor:
                    stats = list(executor.map(compare_dump_pair, dump_pairs))

                for tot, mat, short_tot, short_mat in stats:
                    total_windows += tot
                    matched_windows += mat
                    total_short_frames += short_tot
                    matched_short_frames += short_mat

                pct = (matched_windows / total_windows * 100.0) if total_windows > 0 else 0.0
                short_pct = (matched_short_frames / total_short_frames * 100.0) if total_short_frames > 0 else 0.0
                print(f"{ref_name.upper()} | pad: {pad:4d} | offset: {offset} | windows matched: {matched_windows:5d}/{total_windows:5d} ({pct:6.2f}%) | short agreement: {matched_short_frames}/{total_short_frames} ({short_pct:5.1f}%)")

                sweep_results.append({
                    "pad": pad,
                    "offset": offset,
                    "matched": matched_windows,
                    "total": total_windows,
                    "pct": pct,
                    "matched_short": matched_short_frames,
                    "total_short": total_short_frames,
                    "short_pct": short_pct
                })

                if pct > best_match:
                    best_match = pct
                    best_combo = (pad, offset)

        return sweep_results, best_combo, best_match

    fdk_sweep, fdk_best_combo, fdk_best_pct = eval_sweep("fdk", fdk_pads)
    apple_sweep, apple_best_combo, apple_best_pct = eval_sweep("apple", apple_pads)

    print("\n================ Alignment Summary ================")
    print(f"FDK Best Alignment:   pad={fdk_best_combo[0]}, offset={fdk_best_combo[1]} -> {fdk_best_pct:.2f}% matched")
    print(f"Apple Best Alignment: pad={apple_best_combo[0]}, offset={apple_best_combo[1]} -> {apple_best_pct:.2f}% matched")

    # Analyze remaining misses for best alignments
    def analyze_misses(ref_name, pad, offset):
        inj_dir = ref_dir / f"inj_{ref_name}_p{pad}_o{offset}"
        miss_categories = {"startup_frames": 0, "legal_transitions": 0, "other": 0}
        clip_miss_counts = {}

        for clip_path in clips:
            stem = clip_path.stem
            ref_dump = ref_dir / f"{stem}_{ref_name}48.dump"
            inj_dump = inj_dir / f"{stem}.dump"

            faac_parsed = get_parsed_dump(inj_dump)
            ref_parsed = get_parsed_dump(ref_dump)

            clip_misses = 0
            for frame_idx, chs in faac_parsed.items():
                ref_frame_idx = frame_idx + offset
                ref_chs = ref_parsed.get(ref_frame_idx, {})
                for ch_idx, ics in chs.items():
                    ref_ics = ref_chs.get(ch_idx)
                    if ref_ics is None:
                        continue
                    if ics.win_seq != ref_ics.win_seq:
                        clip_misses += 1
                        if frame_idx <= 2:
                            miss_categories["startup_frames"] += 1
                        elif (ics.win_seq in (1, 3) or ref_ics.win_seq in (1, 3)): # LONG_START or LONG_STOP
                            miss_categories["legal_transitions"] += 1
                        else:
                            miss_categories["other"] += 1
            if clip_misses > 0:
                clip_miss_counts[stem] = clip_misses

        print(f"\nMiss Analysis for {ref_name.upper()} (pad={pad}, offset={offset}):")
        print(f"  Startup frames (f<=2): {miss_categories['startup_frames']}")
        print(f"  Legal transition constraint frames (start/stop windows): {miss_categories['legal_transitions']}")
        print(f"  Other misses: {miss_categories['other']}")
        return miss_categories, clip_miss_counts

    fdk_misses, fdk_clip_misses = analyze_misses("fdk", fdk_best_combo[0], fdk_best_combo[1])
    apple_misses, apple_clip_misses = analyze_misses("apple", apple_best_combo[0], apple_best_combo[1])

    res = {
        "a0_passed": True,
        "fdk": {
            "best_pad": fdk_best_combo[0],
            "best_offset": fdk_best_combo[1],
            "best_pct": fdk_best_pct,
            "pass_95": bool(fdk_best_pct >= 95.0),
            "sweep": fdk_sweep,
            "miss_categories": fdk_misses,
            "clip_misses": fdk_clip_misses
        },
        "apple": {
            "best_pad": apple_best_combo[0],
            "best_offset": apple_best_combo[1],
            "best_pct": apple_best_pct,
            "pass_95": bool(apple_best_pct >= 95.0),
            "sweep": apple_sweep,
            "miss_categories": apple_misses,
            "clip_misses": apple_clip_misses
        }
    }

    out_json = WORK_DIR / "step_a.json"
    out_json.write_text(json.dumps(res, indent=2))
    res_dir = LADDER_DIR / "results/s8w"
    res_dir.mkdir(parents=True, exist_ok=True)
    (res_dir / "step_a.json").write_text(json.dumps(res, indent=2))
    print(f"\nStep A complete. Results saved to {out_json} and probe/ladder/results/s8w/step_a.json")

    if not (fdk_best_pct >= 95.0 and apple_best_pct >= 95.0):
        print("WARNING: One or both references failed the >= 95% window match threshold!")

    return res

if __name__ == "__main__":
    a0_results = run_step_a0()
    a1_results = run_step_a1()
