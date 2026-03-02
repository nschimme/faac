import os
import subprocess
import urllib.request
import zipfile
import shutil
import wave

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

def get_duration(wav_path):
    try:
        with wave.open(wav_path, 'rb') as f:
            frames = f.getnframes()
            rate = f.getframerate()
            return frames / float(rate)
    except:
        return 0

def resample(input_path, output_path, rate, start=0, duration=10):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    # Resample to mono, 16-bit, specified rate, and trim to 3-10s
    # ViSQOL likes 3-10s.
    cmd = [
        "ffmpeg", "-y", "-ss", str(start), "-t", str(duration),
        "-i", input_path,
        "-ar", str(rate), "-ac", "1", "-sample_fmt", "s16",
        output_path
    ]
    subprocess.run(cmd, check=True, capture_output=True)

def setup_pmlt():
    # PMLT is for Audio (48kHz)
    src_dir = os.path.join(TEMP_DIR, "PMLT2014-PMLT2014")
    dest_dir = os.path.join(BASE_DATA_DIR, "audio")

    wav_files = []
    for root, dirs, files in os.walk(src_dir):
        for f in files:
            if f.endswith(".wav"):
                wav_files.append(os.path.join(root, f))

    # Pick 10 samples and trim to 5s from the middle
    for i, wav in enumerate(wav_files[:10]):
        dur = get_duration(wav)
        start = max(0, (dur - 5) / 2)
        output = os.path.join(dest_dir, f"music_{i}.wav")
        print(f"Processing {wav} to 48kHz (5s)...")
        resample(wav, output, 48000, start=start, duration=5)

def setup_tcd_voip():
    # TCD-VOIP is for Speech (16kHz)
    src_dir = os.path.join(TEMP_DIR, "TCD-VOIP-harte2015tcd")
    dest_dir = os.path.join(BASE_DATA_DIR, "speech")

    wav_files = []
    for root, dirs, files in os.walk(src_dir):
        for f in files:
            if f.endswith(".wav") and ("Test Set" in root or "chop" in root):
                wav_files.append(os.path.join(root, f))

    # Pick 10 samples and trim to 5s
    for i, wav in enumerate(wav_files[:10]):
        dur = get_duration(wav)
        start = max(0, (dur - 5) / 2)
        output = os.path.join(dest_dir, f"speech_{i}.wav")
        print(f"Processing {wav} to 16kHz (5s)...")
        resample(wav, output, 16000, start=start, duration=5)

if __name__ == "__main__":
    if os.path.exists(BASE_DATA_DIR):
        print("Datasets already setup.")
    else:
        for name, url in DATASETS.items():
            download_and_extract(name, url)

        setup_pmlt()
        setup_tcd_voip()

        # Cleanup temp
        if os.path.exists(TEMP_DIR):
            shutil.rmtree(TEMP_DIR)
        print("Done.")
