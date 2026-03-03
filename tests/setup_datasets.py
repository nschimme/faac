import os
import subprocess
import urllib.request
import zipfile
import shutil
import wave
import re

DATASETS = {
    "PMLT2014": "https://github.com/nschimme/PMLT2014/archive/refs/tags/PMLT2014.zip",
    "TCD-VOIP": "https://github.com/nschimme/TCD-VOIP/archive/refs/tags/harte2015tcd.zip"
}

BASE_DATA_DIR = "tests/data/external"
TEMP_DIR = "tests/data/temp"

def download_and_extract(name, url):
    os.makedirs(TEMP_DIR, exist_ok=True)
    zip_path = os.path.join(TEMP_DIR, f"{name}.zip")
    if not os.path.exists(zip_path):
        print(f"Downloading {name}...")
        urllib.request.urlretrieve(url, zip_path)

    print(f"Extracting {name}...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(TEMP_DIR)

def get_info(wav_path):
    try:
        with wave.open(wav_path, 'rb') as f:
            frames = f.getnframes()
            rate = f.getframerate()
            channels = f.getnchannels()
            return frames / float(rate), channels
    except:
        return 0, 2

def resample(input_path, output_path, rate, channels, start=0, duration=7):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    cmd = [
        "ffmpeg", "-y", "-ss", str(start), "-t", str(duration),
        "-i", input_path,
        "-ar", str(rate), "-ac", str(channels), "-sample_fmt", "s16",
        output_path
    ]
    subprocess.run(cmd, check=True, capture_output=True)

def setup_pmlt():
    src_dir = os.path.join(TEMP_DIR, "PMLT2014-PMLT2014")
    dest_dir = os.path.join(BASE_DATA_DIR, "audio")

    wav_files = []
    for root, dirs, files in os.walk(src_dir):
        for f in files:
            if f.endswith("48k.wav") and not re.search(r"48k\.\d+\.wav$", f):
                wav_files.append(os.path.join(root, f))

    print(f"Found {len(wav_files)} valid PMLT audio files.")
    for i, wav in enumerate(wav_files):
        dur, chans = get_info(wav)
        start = max(0, (dur - 7) / 2)
        output = os.path.join(dest_dir, f"music_{i}.wav")
        resample(wav, output, 48000, chans, start=start, duration=7)

def setup_tcd_voip():
    src_dir = os.path.join(TEMP_DIR, "TCD-VOIP-harte2015tcd")
    dest_dir = os.path.join(BASE_DATA_DIR, "speech")

    wav_files = []
    for root, dirs, files in os.walk(src_dir):
        for f in files:
            if f.endswith(".wav") and ("Test Set" in root or "chop" in root):
                wav_files.append(os.path.join(root, f))

    print(f"Found {len(wav_files)} valid TCD speech files.")
    for i, wav in enumerate(wav_files):
        dur, chans = get_info(wav)
        start = max(0, (dur - 7) / 2)
        output = os.path.join(dest_dir, f"speech_{i}.wav")
        resample(wav, output, 16000, chans, start=start, duration=7)

if __name__ == "__main__":
    if not os.path.exists(BASE_DATA_DIR):
        for name, url in DATASETS.items():
            download_and_extract(name, url)

        setup_pmlt()
        setup_tcd_voip()

        if os.path.exists(TEMP_DIR):
            shutil.rmtree(TEMP_DIR)
    else:
        print("Datasets already setup.")
    print("Done.")
