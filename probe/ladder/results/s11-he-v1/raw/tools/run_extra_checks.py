#!/usr/bin/env python3
"""Run limited AAC packet-identity and HE-AAC decode-acceptance checks."""

import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

BENCH = Path("/Users/nschimme/gitprojects/faac-benchmark")
sys.path.insert(0, str(BENCH))
from codec_bench.decoders import FAADDecoder, FDKDecoder, FFmpegDecoder  # noqa: E402
from codec_bench.encoders import FAACEncoder  # noqa: E402
from config import CORPUS_GATE  # noqa: E402


ROOT = Path("/private/tmp")
BINARIES = {
    "baseline": ROOT / "faac-he-v1-probes/build-baseline/frontend/faac",
    "noise": ROOT / "faac-he-v1-noise/build-candidate/frontend/faac",
    "limiter": ROOT / "faac-he-v1-candidate/build-candidate/frontend/faac",
}
WORKTREES = {
    "baseline": ROOT / "faac-he-v1-probes",
    "noise": ROOT / "faac-he-v1-noise",
    "limiter": ROOT / "faac-he-v1-candidate",
}
FAAD = ROOT / "faac-he-v1-decoder-build/frontend/faad"
FDKDEC = BENCH / "bin/fdkdec"
FFMPEG = Path("/opt/homebrew/bin/ffmpeg")
FFPROBE = Path("/opt/homebrew/bin/ffprobe")
DATA = BENCH / "data/external/audio"
OUT = ROOT / "faac-he-v1-probes/probe"
CLIPS = CORPUS_GATE["audio_48k"]
RATE_KBPS = (32, 64)


def sha_file(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def source_hash(repo, committed=False):
    paths = subprocess.run(
        ["git", "-C", str(repo), "ls-files", "-z"],
        check=True, capture_output=True,
    ).stdout.split(b"\0")
    h = hashlib.sha256()
    for raw in sorted(p for p in paths if p):
        rel = raw.decode()
        if committed:
            data = subprocess.run(
                ["git", "-C", str(repo), "show", f"HEAD:{rel}"],
                check=True, capture_output=True,
            ).stdout
        else:
            data = (repo / rel).read_bytes()
        h.update(len(raw).to_bytes(8, "big"))
        h.update(raw)
        h.update(len(data).to_bytes(8, "big"))
        h.update(data)
    return h.hexdigest()


def run(cmd, env=None):
    try:
        p = subprocess.run(cmd, env=env, capture_output=True, text=True, check=False)
        return {"returncode": p.returncode, "stdout": p.stdout or "", "stderr": p.stderr or ""}
    except OSError as e:
        return {"returncode": None, "stdout": "", "stderr": str(e)}


def get_packets(path):
    cmd = [str(FFPROBE), "-v", "error", "-select_streams", "a:0", "-show_packets",
           "-show_data", "-show_entries", "packet=size,data", "-of", "json", str(path)]
    p = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if p.returncode or p.stderr:
        raise RuntimeError(f"ffprobe failed ({p.returncode}): {p.stderr.strip()}")
    packets = json.loads(p.stdout).get("packets", [])
    result = []
    for packet in packets:
        chunks = []
        for line in packet["data"].splitlines():
            if ":" not in line:
                continue
            hex_text = line.split(":", 1)[1].split("  ", 1)[0]
            words = []
            for word in hex_text.split():
                if not all(c in "0123456789abcdefABCDEF" for c in word):
                    break
                if len(word) not in (2, 4):
                    break
                words.append(word)
            if words:
                chunks.append(bytes.fromhex("".join(words)))
        data = b"".join(chunks)
        if len(data) != int(packet["size"]):
            raise RuntimeError(f"ffprobe packet hex parse size mismatch: {len(data)} != {packet['size']}")
        result.append(data)
    if not result:
        raise RuntimeError("ffprobe returned no AAC packets")
    return result


def packet_identity(packets):
    h = hashlib.sha256()
    for packet in packets:
        h.update(len(packet).to_bytes(8, "big"))
        h.update(packet)
    return h.hexdigest()


def encode(encoder, source, output, bitrate, control):
    with wave.open(str(source), "rb") as wav:
        channels, rate = wav.getnchannels(), wav.getframerate()
    cmd = encoder.get_encode_cmd(
        str(source), str(output), bitrate, channels, rate, rate_control=control
    )
    result = run(cmd, encoder.get_run_env())
    result["command"] = cmd
    if result["returncode"] != 0 or not output.is_file():
        raise RuntimeError(json.dumps(result))
    return result


def lc_comparison(clip, candidate):
    result = {"clip": clip, "candidate": candidate, "bitrate_kbps": 128, "rate_control": "abr"}
    source = DATA / clip
    try:
        with tempfile.TemporaryDirectory(prefix="faac-lc-check-") as td:
            base_path, cand_path = Path(td) / "base.m4a", Path(td) / "candidate.m4a"
            base_encoder = FAACEncoder("baseline", str(BINARIES["baseline"]), profile="lc")
            cand_encoder = FAACEncoder(candidate, str(BINARIES[candidate]), profile="lc")
            result["baseline_encode"] = encode(base_encoder, source, base_path, 128, "abr")
            result["candidate_encode"] = encode(cand_encoder, source, cand_path, 128, "abr")
            base_packets, cand_packets = get_packets(base_path), get_packets(cand_path)
            equal = len(base_packets) == len(cand_packets) and all(
                a == b for a, b in zip(base_packets, cand_packets)
            )
            result["baseline_packets"] = len(base_packets)
            result["candidate_packets"] = len(cand_packets)
            result["baseline_packet_sha256"] = packet_identity(base_packets)
            result["candidate_packet_sha256"] = packet_identity(cand_packets)
            result["packet_identity"] = equal
            result["first_differing_packet"] = next(
                (i for i, (a, b) in enumerate(zip(base_packets, cand_packets)) if a != b), None
            )
    except Exception as e:
        result["error"] = str(e)
        result["packet_identity"] = False
    return result


def decode_one(decoder, encoded, out_wav):
    cmd = decoder.get_decode_cmd(str(encoded), str(out_wav))
    if isinstance(decoder, FFmpegDecoder):
        cmd[1:1] = ["-v", "error", "-xerror"]
    result = run(cmd, decoder.get_run_env())
    result["command"] = cmd
    result["accepted"] = result["returncode"] == 0
    if isinstance(decoder, FFmpegDecoder) and result["stderr"].strip():
        result["accepted"] = False
    result["output_created"] = out_wav.is_file()
    if not result["output_created"]:
        result["accepted"] = False
    return result


def he_stream(candidate, clip, bitrate):
    result = {"candidate": candidate, "clip": clip, "bitrate_kbps_total": bitrate,
              "profile": "he", "rate_control": "cbr", "decoders": {}}
    source = DATA / clip
    try:
        with tempfile.TemporaryDirectory(prefix="faac-he-check-") as td:
            td = Path(td)
            encoded = td / "stream.m4a"
            encoder = FAACEncoder(candidate, str(BINARIES[candidate]), profile="he")
            result["encode"] = encode(encoder, source, encoded, bitrate, "cbr")
            result["encoded_sha256"] = sha_file(encoded)
            for name, decoder in DECODERS.items():
                result["decoders"][name] = decode_one(decoder, encoded, td / f"{name}.wav")
    except Exception as e:
        result["error"] = str(e)
    return result


DECODERS = {
    "strict_fdk": FDKDecoder("strict FDK", str(FDKDEC), "fdkdec"),
    "faad3_strict": FAADDecoder("FAAD3 strict", str(FAAD), "faad3", is_faad3=True),
    "ffmpeg_strict": FFmpegDecoder("FFmpeg", str(FFMPEG), "ffmpeg"),
}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source_info = {}
    for name, repo in WORKTREES.items():
        library = BINARIES[name].parent.parent / "libfaac/libfaac.2.dylib"
        source_info[name] = {
            "head": subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"],
                                    check=True, capture_output=True, text=True).stdout.strip(),
            "source_tree_sha256": source_hash(repo, committed=(name == "baseline")),
            "binary": str(BINARIES[name]),
            "binary_sha256": sha_file(BINARIES[name]),
            "linked_library": str(library),
            "linked_library_sha256": sha_file(library),
        }
    input_hashes = {clip: sha_file(DATA / clip) for clip in CLIPS}

    lc_jobs = [(clip, cand) for clip in CLIPS for cand in ("noise", "limiter")]
    lc_results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(lc_comparison, clip, cand) for clip, cand in lc_jobs]
        for future in concurrent.futures.as_completed(futures):
            lc_results.append(future.result())
    lc_results.sort(key=lambda x: (x["candidate"], x["clip"]))

    he_jobs = [(cand, clip, rate) for cand in ("noise", "limiter")
               for rate in RATE_KBPS for clip in CLIPS]
    he_results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(he_stream, cand, clip, rate) for cand, clip, rate in he_jobs]
        for future in concurrent.futures.as_completed(futures):
            he_results.append(future.result())
    he_results.sort(key=lambda x: (x["candidate"], x["bitrate_kbps_total"], x["clip"]))

    counts = {
        "lc_packet_comparisons": len(lc_results),
        "lc_packet_identity_pass": sum(x.get("packet_identity", False) for x in lc_results),
        "lc_packet_identity_fail": sum(not x.get("packet_identity", False) for x in lc_results),
        "he_streams": len(he_results),
        "he_encode_fail": sum("error" in x for x in he_results),
    }
    for decoder_name in DECODERS:
        outcomes = [x.get("decoders", {}).get(decoder_name, {}).get("accepted", False)
                    for x in he_results if "error" not in x]
        counts[f"{decoder_name}_pass"] = sum(outcomes)
        counts[f"{decoder_name}_fail"] = len(outcomes) - sum(outcomes) + counts["he_encode_fail"]

    output = {
        "schema": 1,
        "scope": "AAC-LC ABR packet identity and HE-AAC v1 CBR decode acceptance; no MOS or timing claims",
        "source_hash_method": "SHA-256 over sorted tracked paths and contents; baseline from HEAD, candidates from worktree contents",
        "source_builds": source_info,
        "inputs": {"directory": str(DATA), "gate_clips": list(CLIPS), "sha256": input_hashes},
        "decoders": {name: {"binary": decoder.binary_path, "binary_sha256": sha_file(decoder.binary_path)}
                     for name, decoder in DECODERS.items()},
        "he_rates_kbps_total": list(RATE_KBPS),
        "counts": counts,
        "lc_packet_identity": lc_results,
        "he_decode_acceptance": he_results,
    }
    result_path = OUT / "extra-checks.json"
    result_path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    lines = ["FAAC HE-v1 extra checks", json.dumps(counts, sort_keys=True)]
    lines.extend(f"LC {x['candidate']} {x['clip']}: {'PASS' if x.get('packet_identity') else 'FAIL'}"
                 for x in lc_results)
    for x in he_results:
        statuses = ", ".join(f"{k}={'PASS' if v.get('accepted') else 'FAIL'}"
                              for k, v in x.get("decoders", {}).items())
        lines.append(f"HE {x['candidate']} {x['bitrate_kbps_total']} kbps {x['clip']}: {statuses or x.get('error')}")
    (OUT / "extra-checks.log").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {result_path}")
    print(json.dumps(counts, sort_keys=True))
    return 1 if any(v for k, v in counts.items() if k.endswith("_fail")) else 0


if __name__ == "__main__":
    raise SystemExit(main())
