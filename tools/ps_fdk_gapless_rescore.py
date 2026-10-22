#!/usr/bin/env python3
"""Gapless-mux saved FDK ADTS results and rescore without cropping audio."""
import argparse
import concurrent.futures
import json
import subprocess
import sys
import threading
import wave
from pathlib import Path

import ps_quality_ab as qa


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("manifest", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--faam", required=True)
    p.add_argument("--fdk-info", type=Path, required=True)
    p.add_argument("--decoder")
    p.add_argument("--duration-mismatch-only", action="store_true",
                   help="rescore only source rows whose previous scorer reported duration_mismatch")
    p.add_argument("--bitrates", help="comma-separated total bitrate targets to rescore")
    p.add_argument("--jobs", type=int, default=1)
    a = p.parse_args()
    source = json.loads(a.manifest.read_text())
    output_path = a.output / "normalized-results.json"
    try:
        output = json.loads(output_path.read_text())
    except FileNotFoundError:
        output = {"source_manifest": str(a.manifest), "results": {}}
    a.output.mkdir(parents=True, exist_ok=True)
    rows = list(source["results"].items())
    lock = threading.Lock()

    def process(pair):
        key, row = pair
        if row.get("status") != "ok" or row.get("encoder_kind") != "fdk":
            return
        if a.bitrates and str(row["bitrate_kbps_total"]) not in a.bitrates.split(","):
            return
        if a.duration_mismatch_only and "duration_mismatch" not in (row.get("ffmpeg_backend", "") + row.get("fdk_backend", "")):
            return
        if key in output["results"]:
            return
        raw = Path(row["encoded_file"])
        clip = Path(row["clip"])
        with wave.open(str(clip), "rb") as w:
            nframes, rate, channels = w.getnframes(), w.getframerate(), w.getnchannels()
        aot = 29 if row["profile"] == "he-aac-v2" else 5
        info = json.loads(qa.run([str(a.fdk_info), str(aot), str(row["bitrate_kbps_total"] * 1000), str(rate), str(channels)]))
        payload, overhead, frames = qa.adts_payload_bytes(raw)
        core_frame = info["frameLength"] // 2
        delay = info["nDelayCore"] // 2
        padding = frames * core_frame - delay - nframes // 2
        if padding < 0:
            raise RuntimeError(f"negative padding for {clip}: {padding}")
        stem = f"{clip.stem}_{row['profile']}_{row['bitrate_kbps_total']}k_{key[:10]}"
        m4a = a.output / (stem + ".m4a")
        ffwav = a.output / (stem + "_ffmpeg.wav")
        refwav = Path(row["ffmpeg_wav"]).with_name(Path(row["ffmpeg_wav"]).name.replace("_ffmpeg.wav", "_reference.wav"))
        signaling = "ps" if aot == 29 else "compatible"
        qa.run([a.faam, "-i", str(raw), "-o", str(m4a), "--sbr-signaling", signaling,
                "--encoder-delay", str(delay), "--padding-delay", str(padding), "--overwrite"])
        qa.ffmpeg_decode(m4a, ffwav)
        ffmos, ffbackend = qa.score_wav_pair(str(refwav), str(ffwav), mode_str="audio", sample_rate=48000)
        item = {"status": "ok", "raw_adts": str(raw), "gapless_m4a": str(m4a),
                "gapless_sha256": qa.digest(m4a), "payload_bitrate_kbps": payload * 8 / row["duration_s"] / 1000,
                "transport_bitrate_kbps": overhead * 8 / row["duration_s"] / 1000,
                "transport_frames": frames, "encoder_delay_core_samples": delay,
                "padding_core_samples": padding, "nDelayCore": info["nDelayCore"],
                "frame_length_core_samples": core_frame, "ffmpeg_mos": float(ffmos),
                "ffmpeg_backend": ffbackend, "ffmpeg_wav": str(ffwav),
                "ffmpeg_coherence_error": qa.optional_float(qa.coherence_error(str(refwav), str(ffwav)))}
        if a.decoder:
            dwav = a.output / (stem + "_fdk.wav")
            qa.run([a.decoder, str(m4a), str(dwav)])
            mos, backend = qa.score_wav_pair(str(refwav), str(dwav), mode_str="audio", sample_rate=48000)
            item.update(fdk_wav=str(dwav), fdk_mos=float(mos), fdk_backend=backend,
                        fdk_coherence_error=qa.optional_float(qa.coherence_error(str(refwav), str(dwav))))
        with lock:
            output["results"][key] = item
            output_path.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n")
            print(f"{clip.name} | {row['profile']} | {row['bitrate_kbps_total']}k | {ffmos:.4f} | {ffbackend}", flush=True)

    if a.jobs < 1:
        p.error("--jobs must be positive")
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        list(pool.map(process, rows))


if __name__ == "__main__":
    raise SystemExit(main())
