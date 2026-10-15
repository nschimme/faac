#!/usr/bin/env python3
"""Strict HE-AAC smoke matrix for tier17 builds with SBR decimation 4."""

from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import run_envcount as envcount


def main() -> int:
    sys.path.insert(0, str(envcount.BENCH))
    sys.path.insert(0, str(envcount.BENCH / "scripts"))
    from codec_bench.decoders import FDKDecoder
    from codec_bench.encoders import FAACEncoder
    import utils

    base = Path("/private/tmp/faac-envcount-tier17-base/build-tier17-dec4/frontend/faac")
    stack = Path("/private/tmp/faac-envcount-tier17-stack/build-tier17-dec4/frontend/faac")
    output = envcount.DEFAULT_OUTPUT_ROOT / "tier17/decimation4-smoke"
    fixtures = envcount.DEFAULT_OUTPUT_ROOT / "tier17/production-gates/fixtures"
    cases = (("silence_mono.wav", 1, 17), ("tone_stereo.wav", 2, 64),
             ("partial_stereo.wav", 2, 34))
    dec = FDKDecoder("FDK AAC", str(envcount.FDK_BIN))
    rows = []
    for name, binary in (("base", base), ("stack", stack)):
        for fixture, channels, rate in cases:
            source = fixtures / fixture
            encoded = output / "encodes" / name / f"{fixture}.{rate}.m4a"
            decoded = output / "decoded-fdk" / name / f"{fixture}.{rate}.wav"
            encoded.parent.mkdir(parents=True, exist_ok=True)
            decoded.parent.mkdir(parents=True, exist_ok=True)
            encoder = FAACEncoder(f"tier17-decimation4-{name}", str(binary), profile="he")
            cmd = encoder.get_encode_cmd(str(source), str(encoded), rate, channels, 48000,
                                         rate_control="cbr")
            if cmd[cmd.index("--object-type") + 1] != "he-aac-v1" or "--cbr" not in cmd:
                raise RuntimeError(f"not forced HE-AAC v1 CBR: {cmd}")
            clean_env = {k: v for k, v in os.environ.items() if not k.startswith("FAAC_SBR_")}
            envcount.run(cmd, env=clean_env)
            profile = envcount.profile(encoded)
            if profile != "HE-AAC":
                raise RuntimeError(f"unexpected profile {profile}: {encoded}")
            with tempfile.TemporaryDirectory(prefix="tier17-dec4-fdk-") as tmp:
                envcount.decode_fdk(encoded, decoded, dec, utils, Path(tmp))
            faad_wav = output / "decoded-faad" / name / f"{fixture}.{rate}.wav"
            faad_wav.parent.mkdir(parents=True, exist_ok=True)
            faad = envcount.run([str(envcount.FAAD_BIN), "--strict", "--quiet", "--overwrite",
                                 "-o", str(faad_wav), str(encoded)])
            ffmpeg = envcount.run([envcount.FFMPEG, "-v", "error", "-y", "-i", str(encoded), "-f", "null", "-"])
            row = {"variant": name, "fixture": fixture, "channels": channels, "rate_kbps": rate,
                   "profile": profile, "payload_sha256": envcount.payload_identity(encoded),
                   "fdk_pcm_sha256": envcount.sha256(decoded), "faad_pass": faad.returncode == 0,
                   "ffmpeg_pass": ffmpeg.returncode == 0}
            if not row["faad_pass"] or not row["ffmpeg_pass"]:
                raise RuntimeError(f"strict decode failure: {row}")
            rows.append(row)
            print(json.dumps(row, sort_keys=True), flush=True)
    result = {"schema": 1, "sbr_decimation": 4, "rows": rows,
              "all_profiles_he_aac": all(r["profile"] == "HE-AAC" for r in rows),
              "all_fdk_faad_ffmpeg_pass": all(r["faad_pass"] and r["ffmpeg_pass"] for r in rows)}
    output.mkdir(parents=True, exist_ok=True)
    (output / "summary.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
