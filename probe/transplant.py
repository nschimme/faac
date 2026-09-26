#!/usr/bin/env python3
"""Drive the FAAD SBR donor transplant experiment.

The decoder's donor offset is frame based, whereas encoder priming is sample
based.  We measure the latter with a click signal, add leading PCM to FDK's
input when needed, and retain the remaining integral-frame displacement as
FAAD_SBR_DONOR_OFFSET.  The CSV contains both the calibration and per-clip
cross-correlation checks so a score is never detached from its alignment.
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import numpy as np
import scipy.signal
import soundfile as sf


ROOT = Path(__file__).resolve().parent.parent
FAAC = Path("/private/tmp/claude-501/faac-work/master/b/frontend/faac")
FDKAAC = Path("/opt/homebrew/bin/fdkaac")
FAAD = ROOT / "bs/frontend/faad"
SCORER = Path("/Users/nschimme/gitprojects/faac-benchmark/scripts/align/sc.py")
BENCH_PY = Path("/Users/nschimme/gitprojects/faac-benchmark/.venv/bin/python")
FRAME = 2048                         # output samples/ch in dual-rate HE-AAC
FIELDS = {
    "hdr": "hdr",
    "hdr_grid": "hdr,grid",
    "hdr_env": "hdr,env",
    "hdr_grid_env": "hdr,grid,env",
    "hdr_noise_invf": "hdr,noise,invf",
    "hdr_harm": "hdr,harm",
    "all": "hdr,grid,env,noise,invf,harm",
}


def clean_output(path: Path) -> None:
    """FAAC will not overwrite -o; remove only this exact generated file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def run(cmd: list[str], *, env: dict[str, str] | None = None) -> str:
    full_env = os.environ.copy()
    if env:
        full_env.update(env)
    p = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       env=full_env)
    if p.returncode:
        raise RuntimeError("command failed (%d):\n%s\n%s" %
                           (p.returncode, " ".join(cmd), p.stderr[-4000:]))
    return p.stdout


def encode_faac(src: Path, rate: int, dst: Path) -> None:
    clean_output(dst)
    run([str(FAAC), "--object-type", "he-aac-v1", "-b", str(rate), "-o", str(dst), str(src)])


def encode_fdk(src: Path, rate: int, dst: Path) -> None:
    clean_output(dst)
    run([str(FDKAAC), "-p", "5", "-b", str(rate * 1000), "-f", "2", "-o", str(dst), str(src)])


def decode(src: Path, dst: Path, *, donor: Path | None = None, offset: int = 0,
           fields: str | None = None) -> None:
    clean_output(dst)
    env: dict[str, str] = {}
    if donor is not None:
        env = {"FAAD_SBR_DONOR": str(donor), "FAAD_SBR_DONOR_OFFSET": str(offset),
               "FAAD_SBR_DONOR_FIELDS": fields or FIELDS["all"]}
    run([str(FAAD), "-q", "-b", "32f", "-o", str(dst), str(src)], env=env)


def delay_samples(ref: Path, deg: Path, seconds: float = 10.0) -> int:
    """Positive means ``deg``'s content is later than ``ref`` in samples."""
    r, sr = sf.read(ref, dtype="float32", always_2d=True)
    d, dsr = sf.read(deg, dtype="float32", always_2d=True)
    if sr != dsr:
        raise ValueError(f"sample-rate mismatch: {ref}={sr}, {deg}={dsr}")
    n = min(len(r), len(d), int(sr * seconds))
    if n < 256:
        raise ValueError(f"too little audio to align: {ref}, {deg}")
    rm = r[:n].mean(axis=1)
    dm = d[:n].mean(axis=1)
    rm = rm / (np.std(rm) + 1e-12)
    dm = dm / (np.std(dm) + 1e-12)
    # scipy correlate(ref, deg) is negative when deg is delayed.  Invert it
    # so this routine uses the conventional positive-delay sign everywhere.
    corr_lag = int(np.argmax(scipy.signal.correlate(rm, dm, mode="full"))) - (n - 1)
    return -corr_lag


def padded_copy(src: Path, dst: Path, samples: int) -> None:
    audio, sr = sf.read(src, dtype="float32", always_2d=True)
    if sr != 32000 or audio.shape[1] != 2:
        raise ValueError(f"expected 32 kHz stereo WAV: {src} ({sr} Hz, {audio.shape[1]} ch)")
    clean_output(dst)
    pad = np.zeros((samples, audio.shape[1]), dtype=np.float32)
    sf.write(dst, np.concatenate((pad, audio)), sr, subtype="FLOAT")


def click_wav(path: Path) -> None:
    """A deterministic 32 kHz stereo click fixture with two non-periodic hits."""
    clean_output(path)
    x = np.zeros((32000 * 3, 2), dtype=np.float32)
    for start, amp, length in ((4096, 0.95, 191), (20123, -0.75, 127)):
        x[start:start + length, :] += (amp * np.hanning(length))[:, None]
    sf.write(path, x, 32000, subtype="FLOAT")


def calibration(rate: int, out: Path) -> dict[str, int]:
    """Return click-derived delay/padding/frame-offset calibration for one rate."""
    d = out / "click" / str(rate)
    click = d / "click.wav"
    click_wav(click)
    faac_aac, fdk_aac = d / "faac.aac", d / "fdk.aac"
    faac_wav, fdk_wav = d / "faac.wav", d / "fdk.wav"
    encode_faac(click, rate, faac_aac)
    encode_fdk(click, rate, fdk_aac)
    decode(faac_aac, faac_wav)
    decode(fdk_aac, fdk_wav)
    faac_delay = delay_samples(click, faac_wav)
    fdk_delay = delay_samples(click, fdk_wav)
    raw_pad = faac_delay - fdk_delay
    pad = raw_pad
    if pad < 0:
        pad += FRAME * math.ceil((-pad) / FRAME)
    extra_frames = (pad - raw_pad) // FRAME
    assert pad >= 0 and (pad - raw_pad) % FRAME == 0
    return {"click_faac_delay": faac_delay, "click_fdk_delay": fdk_delay,
            "raw_pad": raw_pad, "pad": pad, "pad_frames": extra_frames,
            "donor_offset": extra_frames}  # padding delays fdk by extra_frames: FAAC frame n = fdk frame n+extra_frames


def score(ref: Path, deg: Path) -> float:
    out = run([str(BENCH_PY), str(SCORER), str(ref), str(deg)],
              env={"NUMBA_DISABLE_JIT": "1"}).strip()
    # align/sc.py prints exactly a scalar, but accept a label for local variants.
    vals = re.findall(r"[-+]?\d+(?:\.\d+)?", out)
    if not vals:
        raise RuntimeError(f"scorer produced no score for {deg}: {out!r}")
    return float(vals[-1])


def safe_name(i: int, path: Path) -> str:
    return f"{i:02d}_{re.sub(r'[^A-Za-z0-9._-]+', '_', path.stem).strip('_')}"


def run_clip(index: int, clip: Path, rate: int, out: Path, cal: dict[str, int]) -> list[dict[str, object]]:
    d = out / "clips" / safe_name(index, clip) / str(rate)
    faac_aac, fdk_raw_aac, fdk_aac = d / "faac.aac", d / "fdk_raw.aac", d / "fdk.aac"
    padded = d / "padded.wav"
    faac_wav, fdk_raw_wav, fdk_wav = d / "faac.wav", d / "fdk_raw.wav", d / "fdk.wav"

    encode_faac(clip, rate, faac_aac)
    encode_fdk(clip, rate, fdk_raw_aac)
    decode(faac_aac, faac_wav)
    decode(fdk_raw_aac, fdk_raw_wav)
    faac_delay = delay_samples(clip, faac_wav)
    fdk_raw_delay = delay_samples(clip, fdk_raw_wav)

    padded_copy(clip, padded, int(cal["pad"]))
    encode_fdk(padded, rate, fdk_aac)
    decode(fdk_aac, fdk_wav)
    fdk_padded_delay = delay_samples(clip, fdk_wav)
    expected = faac_delay + FRAME * int(cal["pad_frames"])
    residual = fdk_padded_delay - expected
    alignment_ok = abs(residual) <= 2

    arms: dict[str, Path] = {"faac": faac_wav, "fdk": fdk_wav}
    for arm, fields in FIELDS.items():
        wav = d / f"{arm}.wav"
        decode(faac_aac, wav, donor=fdk_aac, offset=int(cal["donor_offset"]), fields=fields)
        arms[arm] = wav

    rows: list[dict[str, object]] = []
    for arm, wav in arms.items():
        arm_delay = delay_samples(clip, wav)
        rows.append({
            "clip": str(clip), "rate": rate, "arm": arm, "score": score(clip, wav),
            "arm_delay": arm_delay, "faac_delay": faac_delay,
            "fdk_raw_delay": fdk_raw_delay, "fdk_padded_delay": fdk_padded_delay,
            "click_faac_delay": cal["click_faac_delay"], "click_fdk_delay": cal["click_fdk_delay"],
            "raw_pad": cal["raw_pad"], "pad": cal["pad"], "pad_frames": cal["pad_frames"],
            "donor_offset": cal["donor_offset"], "expected_fdk_delay": expected,
            "alignment_residual": residual, "alignment_ok": alignment_ok,
        })
    return rows


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    clean_output(path)
    fields = list(rows[0]) if rows else ["clip", "rate", "arm", "score"]
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)


def summary(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    ans: list[dict[str, object]] = []
    for rate in sorted({int(r["rate"]) for r in rows}):
        by_clip: dict[str, dict[str, float]] = {}
        for r in rows:
            if int(r["rate"]) == rate:
                by_clip.setdefault(str(r["clip"]), {})[str(r["arm"])] = float(r["score"])
        arms = sorted({a for x in by_clip.values() for a in x})
        for arm in arms:
            vals = [x[arm] for x in by_clip.values() if arm in x]
            diffs = [x[arm] - x["faac"] for x in by_clip.values() if arm in x and "faac" in x]
            ans.append({"rate": rate, "arm": arm, "mean": np.mean(vals),
                        "delta_vs_faac": np.mean(diffs) if diffs else float("nan"),
                        "wins": sum(v > .02 for v in diffs),
                        "losses": sum(v < -.02 for v in diffs), "n": len(vals)})
    return ans


def offset_neighbors(index: int, clip: Path, rate: int, out: Path,
                     cal: dict[str, int]) -> list[dict[str, object]]:
    """Score k-1/k/k+1 when all-fields is not close to padded FDK."""
    d = out / "clips" / safe_name(index, clip) / str(rate)
    faac_aac, fdk_aac = d / "faac.aac", d / "fdk.aac"
    fdk_score = score(clip, d / "fdk.wav")
    rows: list[dict[str, object]] = []
    for offset in range(int(cal["donor_offset"]) - 1, int(cal["donor_offset"]) + 2):
        wav = d / f"all_offset_{offset:+d}.wav"
        decode(faac_aac, wav, donor=fdk_aac, offset=offset, fields=FIELDS["all"])
        s = score(clip, wav)
        rows.append({"clip": str(clip), "rate": rate, "offset": offset,
                     "score": s, "delta_vs_fdk": s - fdk_score,
                     "lag": delay_samples(clip, wav)})
    return rows


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--glob", default="/Users/nschimme/gitprojects/faac-benchmark/data/external/audio_32k/*.wav")
    p.add_argument("--rates", default="64,96", help="comma-separated kb/s values")
    p.add_argument("-j", type=int, default=8, help="parallel clips")
    p.add_argument("--limit", type=int, help="only the first N sorted clips (for smoke runs)")
    p.add_argument("--out", type=Path, default=ROOT / "probe" / "transplant-run")
    a = p.parse_args()
    for tool in (FAAC, FDKAAC, FAAD, SCORER, BENCH_PY):
        if not tool.exists(): p.error(f"missing tool: {tool}")
    clips = sorted(Path(x) for x in __import__("glob").glob(a.glob))
    if a.limit is not None: clips = clips[:a.limit]
    if not clips: p.error(f"no clips matched {a.glob}")
    rates = [int(x) for x in a.rates.split(",") if x.strip()]
    all_rows: list[dict[str, object]] = []
    calibrations: dict[int, dict[str, int]] = {}
    for rate in rates:
        cal = calibration(rate, a.out)
        calibrations[rate] = cal
        print(f"rate {rate}: click faac={cal['click_faac_delay']} fdk={cal['click_fdk_delay']} "
              f"raw_pad={cal['raw_pad']} pad={cal['pad']} frames={cal['pad_frames']} offset={cal['donor_offset']}", flush=True)
        with ThreadPoolExecutor(max_workers=a.j) as ex:
            fs = {ex.submit(run_clip, i, clip, rate, a.out, cal): clip for i, clip in enumerate(clips)}
            for f in as_completed(fs):
                clip = fs[f]
                rows = f.result()
                all_rows.extend(rows)
                probe = rows[0]
                print(f"{rate} {clip.name}: faac_lag={probe['faac_delay']} fdk_raw={probe['fdk_raw_delay']} "
                      f"fdk_padded={probe['fdk_padded_delay']} residual={probe['alignment_residual']} "
                      f"ok={probe['alignment_ok']}", flush=True)
    write_csv(a.out / "results.csv", all_rows)
    sums = summary(all_rows)
    write_csv(a.out / "summary.csv", sums)
    neighbors: list[dict[str, object]] = []
    for rate in rates:
        mean = {str(x["arm"]): float(x["mean"]) for x in sums if int(x["rate"]) == rate}
        if "all" in mean and "fdk" in mean and abs(mean["all"] - mean["fdk"]) > .03:
            print(f"sanity: all differs from fdk by {mean['all'] - mean['fdk']:+.4f}; testing offset neighbors", flush=True)
            neighbors.extend(offset_neighbors(0, clips[0], rate, a.out,
                                              calibrations[rate]))
    if neighbors:
        write_csv(a.out / "offset-neighbors.csv", neighbors)
        for x in neighbors:
            print("neighbor {rate} offset={offset:+d} score={score:.4f} delta_vs_fdk={delta_vs_fdk:+.4f} lag={lag}".format(**x))
    for row in sums:
        print("{rate:>3} {arm:<18} mean={mean:.4f} delta={delta_vs_faac:+.4f} "
              "W/L={wins}/{losses} n={n}".format(**row))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"transplant.py: {e}", file=sys.stderr)
        raise SystemExit(1)
