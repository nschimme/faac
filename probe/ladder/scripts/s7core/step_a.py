#!/usr/bin/env python3
import os
import sys
import pathlib
import subprocess
import re
import numpy as np
import soundfile as sf
import json

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

def decode_to_pcm(m4a_path, pcm_wav):
    cmd = ["ffmpeg", "-v", "error", "-y", "-i", str(m4a_path), "-f", "wav", str(pcm_wav)]
    subprocess.run(cmd, check=True)

def dump_adts_with_faad(m4a_path, dump_txt, pcm_wav):
    env = dict(os.environ, FAAD_LADDER_DUMP="1", FAAD_DUMP=str(dump_txt))
    cmd = [FAAD_BIN, "-o", str(pcm_wav), str(m4a_path)]
    subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)

def run_step_a0():
    print("=== Step A0: Base F encodes & Self-Injection Control ===")
    clips = get_audio_clips()
    base_dir = WORK_DIR / "a0_base"
    base_dir.mkdir(parents=True, exist_ok=True)

    a0_results = {}
    pms_ident_count = 0

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

        # 4. Self-inject F's own dump with win
        a0_m4a = base_dir / f"{stem}_A0.m4a"

        inj_env = dict(
            os.environ,
            **BASE_ENV,
            FAAC_CORE_INJECT=str(f_ci),
            FAAC_CORE_INJECT_FIELDS="win",
            FAAC_CORE_INJECT_OFFSET="1",
            FAAC_CORE_INJECT_LOOSE_SFB="1",
            FAAC_CORE_INJECT_DEBUG="1"
        )
        subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(a0_m4a), str(clip_path)], env=inj_env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)

        # Decode both F and A0 to PCM and compare
        f_pcm_path = base_dir / f"{stem}_F48_dec.wav"
        a0_pcm_path = base_dir / f"{stem}_A0_dec.wav"
        if not f_pcm_path.exists():
            decode_to_pcm(f_m4a, f_pcm_path)
        decode_to_pcm(a0_m4a, a0_pcm_path)

        d_f, _ = sf.read(f_pcm_path)
        d_a0, _ = sf.read(a0_pcm_path)

        is_ident = np.array_equal(d_f, d_a0)
        if is_ident:
            pms_ident_count += 1
        else:
            max_diff = np.max(np.abs(d_f - d_a0))
            print(f"Clip {stem} NOT identical, max diff: {max_diff}")

        a0_results[stem] = {
            "ident": bool(is_ident),
            "bytes_F": f_m4a.stat().st_size,
            "bytes_A0": a0_m4a.stat().st_size
        }

    print(f"Step A0 Result: PCM identical on {pms_ident_count}/{len(clips)} clips.")
    return a0_results, pms_ident_count

def run_step_a1():
    print("\n=== Step A1: Reference Alignment & Window Sequence Verification ===")
    clips = get_audio_clips()
    ref_dir = WORK_DIR / "a1_ref"
    ref_dir.mkdir(parents=True, exist_ok=True)

    fdk_total_frames = 0
    fdk_matched_frames = 0
    fdk_misses_detail = {}

    apple_total_frames = 0
    apple_matched_frames = 0
    apple_misses_detail = {}

    for clip_path in clips:
        stem = clip_path.stem

        # --- FDK ---
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

        # Padded input for fdk injection (2015 samples)
        fdk_padded_wav = ref_dir / f"{stem}_fdk_padded.wav"
        if not fdk_padded_wav.exists():
            pad_wav(clip_path, 2015, fdk_padded_wav)

        fdk_inj_m4a = ref_dir / f"{stem}_fdk_inj.m4a"
        fdk_inj_dump = ref_dir / f"{stem}_fdk_inj.dump"
        fdk_inj_wav = ref_dir / f"{stem}_fdk_inj.wav"

        env_fdk = dict(
            os.environ,
            **BASE_ENV,
            FAAC_CORE_INJECT=str(fdk_ci),
            FAAC_CORE_INJECT_FIELDS="win",
            FAAC_CORE_INJECT_OFFSET="1",
            FAAC_CORE_INJECT_LOOSE_SFB="1",
            FAAC_CORE_INJECT_DEBUG="1"
        )
        subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(fdk_inj_m4a), str(fdk_padded_wav)], env=env_fdk, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
        dump_adts_with_faad(fdk_inj_m4a, fdk_inj_dump, fdk_inj_wav)

        # Verify fdk window sequence match
        faac_fdk_parsed = pd.parse(str(fdk_inj_dump))
        fdk_ref_parsed = pd.parse(str(fdk_dump))

        c_tot = 0
        c_mat = 0
        c_misses = []
        for frame_idx, chs in faac_fdk_parsed.items():
            for ch_idx, ics in chs.items():
                ref_ics = fdk_ref_parsed.get(frame_idx + 1, {}).get(ch_idx)
                if ref_ics is None:
                    continue
                c_tot += 1
                if ics.win_seq == ref_ics.win_seq:
                    c_mat += 1
                else:
                    c_misses.append((frame_idx, ch_idx, ics.win_seq, ref_ics.win_seq))

        fdk_total_frames += c_tot
        fdk_matched_frames += c_mat
        if c_misses:
            fdk_misses_detail[stem] = len(c_misses)

        # --- APPLE ---
        apple_m4a = REF_APPLE_DIR / f"{stem}.m4a"
        apple_dump = ref_dir / f"{stem}_apple48.dump"
        apple_ci = ref_dir / f"{stem}_apple48.ci"
        apple_wav = ref_dir / f"{stem}_apple48.wav"

        if not apple_dump.exists():
            dump_adts_with_faad(apple_m4a, apple_dump, apple_wav)
        if not apple_ci.exists():
            subprocess.run([sys.executable, str(H_CONV), str(apple_dump), str(apple_ci)], check=True)

        # Padded input for apple injection (96 samples)
        apple_padded_wav = ref_dir / f"{stem}_apple_padded.wav"
        if not apple_padded_wav.exists():
            pad_wav(clip_path, 96, apple_padded_wav)

        apple_inj_m4a = ref_dir / f"{stem}_apple_inj.m4a"
        apple_inj_dump = ref_dir / f"{stem}_apple_inj.dump"
        apple_inj_wav = ref_dir / f"{stem}_apple_inj.wav"

        env_apple = dict(
            os.environ,
            **BASE_ENV,
            FAAC_CORE_INJECT=str(apple_ci),
            FAAC_CORE_INJECT_FIELDS="win",
            FAAC_CORE_INJECT_OFFSET="2",
            FAAC_CORE_INJECT_LOOSE_SFB="1",
            FAAC_CORE_INJECT_DEBUG="1"
        )
        subprocess.run([FAAC_BIN, "--overwrite", "-b", "48", "-o", str(apple_inj_m4a), str(apple_padded_wav)], env=env_apple, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=True)
        dump_adts_with_faad(apple_inj_m4a, apple_inj_dump, apple_inj_wav)

        # Verify apple window sequence match
        faac_apple_parsed = pd.parse(str(apple_inj_dump))
        apple_ref_parsed = pd.parse(str(apple_dump))

        a_tot = 0
        a_mat = 0
        a_misses = []
        for frame_idx, chs in faac_apple_parsed.items():
            for ch_idx, ics in chs.items():
                ref_ics = apple_ref_parsed.get(frame_idx + 2, {}).get(ch_idx)
                if ref_ics is None:
                    continue
                a_tot += 1
                if ics.win_seq == ref_ics.win_seq:
                    a_mat += 1
                else:
                    a_misses.append((frame_idx, ch_idx, ics.win_seq, ref_ics.win_seq))

        apple_total_frames += a_tot
        apple_matched_frames += a_mat
        if a_misses:
            apple_misses_detail[stem] = len(a_misses)

    print(f"Step A1 FDK Window Match:   {fdk_matched_frames}/{fdk_total_frames} ({fdk_matched_frames/fdk_total_frames*100:.2f}%)")
    print(f"Step A1 Apple Window Match: {apple_matched_frames}/{apple_total_frames} ({apple_matched_frames/apple_total_frames*100:.2f}%)")

    return {
        "fdk": {"matched": fdk_matched_frames, "total": fdk_total_frames, "misses": fdk_misses_detail},
        "apple": {"matched": apple_matched_frames, "total": apple_total_frames, "misses": apple_misses_detail}
    }

if __name__ == "__main__":
    a0_res, a0_ident = run_step_a0()
    a1_res = run_step_a1()

    out_json = WORK_DIR / "step_a_results.json"
    out_json.write_text(json.dumps({"a0": a0_res, "a1": a1_res}, indent=2))
    print("Step A complete. Saved to", out_json)
