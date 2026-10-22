#!/usr/bin/env python3
"""Diagnostic profile/bitrate sweep for parametric-stereo quality."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import wave

DEFAULT_BENCH = Path(__file__).resolve().parents[2] / "faac-benchmark"
_boot = argparse.ArgumentParser(add_help=False)
_boot.add_argument("--benchmark-dir", default=os.environ.get("FAAC_BENCHMARK_DIR", str(DEFAULT_BENCH)))
_boot_args, _ = _boot.parse_known_args()
BENCH = Path(_boot_args.benchmark_dir).expanduser().resolve()
sys.path.insert(0, str(BENCH))
from phase2_mos import score_wav_pair
from phase3_stereo import coherence_error
from utils import wav_conv

MANIFEST_LOCK = threading.Lock()


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run(cmd):
    p = subprocess.run(cmd, text=True, capture_output=True)
    if p.returncode:
        raise RuntimeError(f"command failed ({p.returncode}): {' '.join(map(str, cmd))}\n{p.stderr[-2000:]}")
    return p.stdout


def file_info(path):
    streams = probe(path, "stream=sample_rate,channels").get("streams", [])
    if not streams:
        raise RuntimeError(f"no audio stream found in {path}")
    return {"sample_rate": int(streams[0]["sample_rate"]), "channels": int(streams[0]["channels"])}


def version_line(cmd):
    try:
        p = subprocess.run(cmd, text=True, capture_output=True, timeout=10)
        return ((p.stdout or p.stderr).splitlines() or [""])[0]
    except Exception as e:
        return f"unavailable: {e}"


def parse_env(spec):
    label, sep, setting = spec.partition("=")
    key, sep2, value = setting.partition("=")
    if not sep or not sep2 or not label or not key:
        raise argparse.ArgumentTypeError("environment override must be LABEL=KEY=VALUE")
    return label, key, value


def optional_float(value):
    return None if value is None else float(value)


def fmt(value, digits=5):
    return "n/a" if value is None else f"{value:.{digits}f}"


def probe(path, entries, select=None):
    cmd = ["ffprobe", "-v", "error"]
    if select:
        cmd += ["-select_streams", select]
    cmd += ["-show_entries", entries, "-of", "json", str(path)]
    return json.loads(run(cmd))


def ffmpeg_decode(src, dst):
    run(["ffmpeg", "-v", "error", "-y", "-i", str(src), "-ar", "48000", "-ac", "2", "-f", "wav", str(dst)])


def adts_payload_bytes(path):
    """Return raw AAC payload bytes and transport overhead for single-block ADTS."""
    data = Path(path).read_bytes()
    pos = payload = frames = 0
    while pos < len(data):
        if pos + 7 > len(data) or data[pos] != 0xFF or (data[pos + 1] & 0xF6) != 0xF0:
            raise RuntimeError(f"invalid or truncated ADTS frame at byte {pos} in {path}")
        header_bytes = 7 if data[pos + 1] & 1 else 9
        if pos + header_bytes > len(data):
            raise RuntimeError(f"truncated ADTS header at byte {pos} in {path}")
        frame_bytes = ((data[pos + 3] & 0x03) << 11) | (data[pos + 4] << 3) | (data[pos + 5] >> 5)
        raw_blocks = (data[pos + 6] & 0x03) + 1
        if raw_blocks != 1:
            raise RuntimeError(f"ADTS frame at byte {pos} contains {raw_blocks} raw blocks; cannot safely account for headers")
        if frame_bytes < header_bytes or pos + frame_bytes > len(data):
            raise RuntimeError(f"invalid/truncated ADTS frame length {frame_bytes} at byte {pos} in {path}")
        payload += frame_bytes - header_bytes
        frames += 1
        pos += frame_bytes
    if pos != len(data) or not frames:
        raise RuntimeError(f"ADTS stream has trailing bytes or no frames: {path}")
    return payload, len(data) - payload, frames


def parse_encoder(spec):
    label, sep, path = spec.partition("=")
    if not sep or not label or not path or not Path(path).is_file():
        raise argparse.ArgumentTypeError("encoder must be LABEL=existing-path")
    return label, str(Path(path).resolve())


def load_manifest(path):
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        return {"version": 1, "results": {}}


def save_manifest(path, data):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    tmp.replace(path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--benchmark-dir", default=str(BENCH))
    p.add_argument("--encoder", action="append", required=True, type=parse_encoder, metavar="LABEL=PATH")
    p.add_argument("--encoder-kind", action="append", default=[], metavar="LABEL=faac|fdk",
                   help="encoder CLI kind (default: faac); FDK uses aac-enc positional input/output syntax")
    p.add_argument("--env", action="append", default=[], type=parse_env, metavar="LABEL=KEY=VALUE")
    p.add_argument("--clips", nargs="+", required=True)
    p.add_argument("--bitrates", default="12,16,20,24")
    p.add_argument("--profiles", default="he-aac-v1,he-aac-v2")
    p.add_argument("--output-dir", required=True)
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--decoder", help="optional FDK decoder executable (input.m4a output.wav)")
    p.add_argument("--faam", help="FAAM muxer used to add FDK gapless metadata")
    p.add_argument("--fdk-info", help="compiled tools/ps_fdk_info helper; compiled automatically when omitted")
    a = p.parse_args()
    if a.jobs < 1:
        p.error("--jobs must be positive")
    out = Path(a.output_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest_path = out / "results.json"
    manifest = load_manifest(manifest_path)
    kinds = {}
    for spec in a.encoder_kind:
        label, sep, kind = spec.partition("=")
        if not sep or not label or kind not in ("faac", "fdk"):
            p.error("--encoder-kind must be LABEL=faac or LABEL=fdk")
        if label in kinds:
            p.error(f"duplicate --encoder-kind for {label}")
        kinds[label] = kind
    encoder_labels = [label for label, _ in a.encoder]
    unknown_kinds = sorted(set(kinds) - set(encoder_labels))
    if unknown_kinds:
        p.error("--encoder-kind label has no matching --encoder: " + ", ".join(unknown_kinds))
    encoders = [(label, path, digest(path), kinds.get(label, "faac")) for label, path in a.encoder]
    fdk_used = any(kind == "fdk" for _, _, _, kind in encoders)
    fdk_info = Path(a.fdk_info).resolve() if a.fdk_info else None
    if fdk_used:
        if not a.faam:
            p.error("FDK encoding requires --faam to mux gapless metadata")
        if fdk_info is None:
            helper_dir = out / "tools"
            helper_dir.mkdir(exist_ok=True)
            fdk_info = helper_dir / "ps_fdk_info"
            if not fdk_info.is_file() or fdk_info.stat().st_mtime < (Path(__file__).parent / "ps_fdk_info.c").stat().st_mtime:
                cflags = run(["pkg-config", "--cflags", "fdk-aac"]).split()
                libs = run(["pkg-config", "--libs", "fdk-aac"]).split()
                libdir = run(["pkg-config", "--variable=libdir", "fdk-aac"]).strip()
                run([os.environ.get("CC", "cc"), *cflags, str(Path(__file__).parent / "ps_fdk_info.c"),
                     *libs, f"-Wl,-rpath,{libdir}", "-o", str(fdk_info)])
        if not fdk_info.is_file():
            p.error(f"FDK info helper not found: {fdk_info}")
    clips = [(str(Path(c).resolve()), digest(c)) for c in a.clips]
    envs = {}
    for label, key, value in a.env:
        envs.setdefault(label, {})[key] = value
    scoring_hashes = {name: digest(BENCH / f"{name}.py") for name in ("phase2_mos", "phase3_stereo", "utils")}
    harness_hash = digest(__file__)
    ffmpeg_version = version_line(["ffmpeg", "-version"])
    decoder_version = version_line([a.decoder, "--version"]) if a.decoder else None
    bitrates = [int(x) for x in a.bitrates.split(",")]
    profiles = [x.strip() for x in a.profiles.split(",") if x.strip()]
    if not profiles or not bitrates or any(b <= 0 for b in bitrates):
        p.error("profiles and positive bitrates are required")

    def work(encoder, clip, profile, bitrate):
        label, binary, binary_hash, encoder_kind = encoder
        clip_path, clip_hash = clip
        opts = {"encoder": label, "encoder_kind": encoder_kind,
                "binary_sha256": binary_hash, "input_sha256": clip_hash,
                "binary_path": binary, "input_path": clip_path,
                "profile": profile, "bitrate_kbps_total": bitrate, "environment": envs.get(label, {}),
                "decoder_sha256": digest(shutil.which(a.decoder) or a.decoder) if a.decoder else None,
                "decoder_version": decoder_version, "ffmpeg_version": ffmpeg_version,
                "faam": str(Path(a.faam).resolve()) if a.faam else None,
                "fdk_info": str(fdk_info) if encoder_kind == "fdk" else None,
                "fdk_info_sha256": digest(fdk_info) if encoder_kind == "fdk" else None,
                "scoring_code_sha256": scoring_hashes, "harness_sha256": harness_hash}
        key = hashlib.sha256(json.dumps(opts, sort_keys=True).encode()).hexdigest()
        cached = manifest["results"].get(key)
        if cached and cached.get("status") == "ok" and all(Path(cached.get(k, "")).is_file() for k in ("m4a", "ffmpeg_wav")):
            return key, cached
        stem = f"{label}_{Path(clip_path).stem}_{profile}_{bitrate}k_{key[:10]}"
        encoded = out / (stem + (".aac" if encoder_kind == "fdk" else ".m4a"))
        playable = out / (stem + ("_gapless.m4a" if encoder_kind == "fdk" else ".m4a"))
        ffwav = out / (stem + "_ffmpeg.wav")
        enc_log = out / (stem + "_encoder.log")
        result = {**opts, "clip": clip_path, "status": "error", "m4a": str(playable),
                  "encoded_file": str(encoded),
                  "ffmpeg_wav": str(ffwav), "encoder_log": str(enc_log)}
        try:
            if not encoded.is_file():
                if encoder_kind == "faac":
                    command = [binary, "-b", str(bitrate), "--object-type", profile,
                               "-o", str(encoded), clip_path]
                else:
                    aot = {"he-aac-v1": "5", "he-aac-v2": "29"}.get(profile)
                    if aot is None:
                        raise RuntimeError(f"FDK encoder does not support profile {profile}")
                    command = [binary, "-r", str(bitrate * 1000), "-t", aot, "-a", "1",
                               clip_path, str(encoded)]
                enc_env = dict(os.environ)
                enc_env.update(envs.get(label, {}))
                enc_proc = subprocess.run(command, text=True, capture_output=True, env=enc_env)
                enc_log.write_text(enc_proc.stderr or "")
                if enc_proc.returncode:
                    raise RuntimeError(f"encoder exited {enc_proc.returncode}; see {enc_log}\n{(enc_proc.stderr or '')[-2000:]}")
            if encoder_kind == "fdk":
                with wave.open(clip_path, "rb") as wf:
                    input_frames = wf.getnframes()
                    input_rate = wf.getframerate()
                    input_channels = wf.getnchannels()
                aot = 29 if profile == "he-aac-v2" else 5
                info = json.loads(run([str(fdk_info), str(aot), str(bitrate * 1000), str(input_rate), str(input_channels)]))
                payload, transport_overhead, transport_frames = adts_payload_bytes(encoded)
                core_frame = int(info["frameLength"]) // 2
                delay_core = int(info["nDelayCore"]) // 2
                input_core_frames = input_frames * (input_rate // 2) // input_rate
                padding_core = transport_frames * core_frame - delay_core - input_core_frames
                if padding_core < 0:
                    raise RuntimeError(f"negative FDK padding ({padding_core}) from measured frame/delay metadata")
                signaling = "ps" if profile == "he-aac-v2" else "compatible"
                run([a.faam, "-i", str(encoded), "-o", str(playable), "--sbr-signaling", signaling,
                     "--encoder-delay", str(delay_core), "--padding-delay", str(padding_core), "--overwrite"])
            else:
                payload = transport_overhead = transport_frames = 0
                playable = encoded
            ffmpeg_decode(playable, ffwav)
            duration = float(probe(clip_path, "format=duration")["format"]["duration"])
            if encoder_kind != "fdk":
                packets = probe(encoded, "packet=size", "a:0").get("packets", [])
                payload = sum(int(x["size"]) for x in packets if "size" in x)
                transport_overhead = encoded.stat().st_size - payload
                transport_frames = len(packets)
            file_bits = playable.stat().st_size * 8
            refwav = out / (stem + "_reference.wav")
            if not refwav.exists():
                if not wav_conv(clip_path, str(refwav), rate=48000, channels=2):
                    raise RuntimeError("utils.wav_conv failed for reference")
            mos, backend = score_wav_pair(str(refwav), str(ffwav), mode_str="audio", sample_rate=48000)
            c_error = optional_float(coherence_error(str(refwav), str(ffwav)))
            result.update(status="ok", duration_s=duration,
                          input_audio=file_info(clip_path), encoded_audio=file_info(playable),
                          payload_bitrate_kbps=payload * 8 / duration / 1000,
                          transport_overhead_bytes=transport_overhead,
                          transport_frame_count=transport_frames,
                          gapless_metadata=({"encoder_delay_core_samples": delay_core,
                                             "padding_core_samples": padding_core,
                                             "frame_length_output_samples": int(info["frameLength"]),
                                             "frame_length_core_samples": core_frame,
                                             "nDelay_core_samples": int(info["nDelayCore"]),
                                             "aot": aot, "faam_sbr_signaling": signaling}
                                            if encoder_kind == "fdk" else None),
                          transport_bitrate_kbps=transport_overhead * 8 / duration / 1000,
                          file_bitrate_kbps=file_bits / duration / 1000,
                          ffmpeg_mos=float(mos), ffmpeg_backend=backend,
                          coherence_error=c_error)
            if a.decoder:
                fdkwav = out / (stem + "_fdk.wav")
                run([a.decoder, str(playable), str(fdkwav)])
                dmos, dbackend = score_wav_pair(str(refwav), str(fdkwav), mode_str="audio", sample_rate=48000)
                result.update(fdk_wav=str(fdkwav), fdk_mos=float(dmos), fdk_backend=dbackend,
                              fdk_coherence_error=optional_float(coherence_error(str(refwav), str(fdkwav))))
            result["m4a_sha256"] = digest(playable)
            result["encoded_sha256"] = digest(encoded)
        except Exception as e:
            result["error"] = str(e)
        with MANIFEST_LOCK:
            manifest["results"][key] = result
            save_manifest(manifest_path, manifest)
        return key, result

    jobs = [(e, c, prof, br) for e in encoders for c in clips for prof in profiles for br in bitrates]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        rows = list(pool.map(lambda x: work(*x), jobs))
    # Preserve pre-existing cached rows and make the final write atomic.
    save_manifest(manifest_path, manifest)
    print("clip | encoder | profile | kbps | AAC payload/transport/file kbps | MOS (ffmpeg) | coherence | status")
    for _, r in rows:
        if r["status"] == "ok":
            print(f"{Path(r['clip']).name} | {r['encoder']} | {r['profile']} | {r['bitrate_kbps_total']} | "
                f"{r['payload_bitrate_kbps']:.2f}/{r['transport_bitrate_kbps']:.2f}/{r['file_bitrate_kbps']:.2f} | {r['ffmpeg_mos']:.4f} | "
                  f"{fmt(r['coherence_error'])} | ok")
        else:
            print(f"{Path(r['clip']).name} | {r['encoder']} | {r['profile']} | {r['bitrate_kbps_total']} | ERROR: {r.get('error')}")
    good = [r for _, r in rows if r["status"] == "ok"]
    if good:
        print("\nMeans by encoder/profile/bitrate (across clips)")
        groups = {}
        for r in good:
            groups.setdefault((r["encoder"], r["profile"], r["bitrate_kbps_total"]), []).append(r)
        for (label, profile, bitrate), group in sorted(groups.items()):
            n = len(group)
            coh = [r["coherence_error"] for r in group if r["coherence_error"] is not None]
            print(f"{label} | {profile} | {bitrate}k | n={n} | "
                  f"payload {sum(r['payload_bitrate_kbps'] for r in group)/n:.2f} kbps | "
                f"transport {sum(r['transport_bitrate_kbps'] for r in group)/n:.2f} kbps | "
                  f"file {sum(r['file_bitrate_kbps'] for r in group)/n:.2f} kbps | "
                  f"MOS {sum(r['ffmpeg_mos'] for r in group)/n:.4f} | "
                  f"coherence {fmt(sum(coh)/len(coh) if coh else None)}")
    return 1 if len(good) != len(rows) else 0


if __name__ == "__main__":
    raise SystemExit(main())
