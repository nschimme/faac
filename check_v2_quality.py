import os
import subprocess
from visqol import VisqolApi

REF_WAV = "/opt/faac-benchmark/data/external/audio/NewYorkCity.16b48k.wav"
FAAC_BIN = "./build/frontend/faac"

modes = {
    "lc": ["--object-type", "lc"],
    "he_v1": ["--object-type", "he-aac-v1"],
    "he_v2": ["--object-type", "he-aac-v2"]
}

# Initialize Visqol
api = VisqolApi()
api.create(mode="audio")

for mode_name, args in modes.items():
    aac_file = f"test_{mode_name}.aac"
    wav_file = f"test_{mode_name}.wav"

    # 1. Encode
    cmd_enc = [FAAC_BIN, "-b", "24"] + args + ["-o", aac_file, REF_WAV]
    subprocess.run(cmd_enc, check=True, capture_output=True)

    # 2. Decode
    cmd_dec = ["ffmpeg", "-y", "-i", aac_file, "-ar", "48000", "-ac", "2", "-sample_fmt", "s16", wav_file]
    subprocess.run(cmd_dec, check=True, capture_output=True)

    # 3. Compute MOS
    try:
        res = api.measure(REF_WAV, wav_file)
        mos = float(res.moslqo)
        print(f"Mode: {mode_name:10} -> MOS: {mos:.4f}")
    except Exception as e:
        print(f"Failed to measure {mode_name}: {e}")
