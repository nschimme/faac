import ffmpeg
import subprocess
import sys
import os
import tempfile
import json

def get_audio_info(path):
    try:
        probe = ffmpeg.probe(path)
        return next(s for s in probe['streams'] if s['codec_type'] == 'audio')
    except Exception as e:
        print(f"Error probing {path}: {e}")
        return None

def run_test(faac_path, name, gen_fn, faac_args, expected_rate=None, verify_decoding=True, use_stdio=False):
    print(f"--- Running Test: {name} ---")
    with tempfile.TemporaryDirectory() as out_dir:
        wav_file = os.path.join(out_dir, "input.wav")
        aac_file = os.path.join(out_dir, "output.aac")
        dec_file = os.path.join(out_dir, "decoded.wav")

        try:
            gen_fn(wav_file)

            if use_stdio:
                print(f"Encoding via stdin/stdout...")
                with open(wav_file, "rb") as fin, open(aac_file, "wb") as fout:
                    subprocess.run([faac_path, "-o", "-", "-"], stdin=fin, stdout=fout, check=True)
            else:
                cmd = [faac_path, "-o", aac_file] + faac_args + [wav_file]
                print(f"Encoding: {' '.join(cmd)}")
                subprocess.run(cmd, check=True, capture_output=True)

            if not os.path.exists(aac_file) or os.path.getsize(aac_file) == 0:
                print(f"Error: Output AAC file is empty or not created.")
                return None

            # Skip bitstream validation for RAW output as ffprobe cannot identify it without extra flags
            if "-r" not in faac_args and "--raw" not in faac_args:
                # Validate bitstream with ffprobe even if not decoding
                info = get_audio_info(aac_file)
                if not info:
                    print(f"Error: ffprobe failed to recognize output as a valid audio stream.")
                    return None

                if info['codec_name'].lower() != 'aac':
                    print(f"Error: Expected aac codec, got {info['codec_name']}")
                    return None

            if verify_decoding:
                print(f"Decoding back with ffmpeg...")
                (
                    ffmpeg
                    .input(aac_file)
                    .output(dec_file)
                    .overwrite_output()
                    .run(quiet=True)
                )

                if not os.path.exists(dec_file) or os.path.getsize(dec_file) == 0:
                    print(f"Error: Decoded WAV file is empty or not created.")
                    return None

                if expected_rate:
                    dec_info = get_audio_info(dec_file)
                    actual_rate = int(dec_info['sample_rate'])
                    if actual_rate != expected_rate:
                        print(f"Error: Expected sample rate {expected_rate}, got {actual_rate}")
                        return None

            size = os.path.getsize(aac_file)
            print(f"Test {name} passed ({size} bytes).")
            return size

        except ffmpeg.Error as e:
            print(f"FFmpeg error: {e.stderr.decode() if e.stderr else str(e)}")
            return None
        except subprocess.CalledProcessError as e:
            print(f"FAAC error: {e.stderr.decode() if e.stderr else str(e)}")
            return None
        except Exception as e:
            print(f"Unexpected error: {e}")
            return None

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path_to_faac>")
        sys.exit(1)

    faac_path = sys.argv[1]

    # Check if ffmpeg is installed
    try:
        subprocess.run(["ffmpeg", "-version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("FFmpeg not found. Aborting encoder tests.")
        sys.exit(1)

    # (name, gen_fn, faac_args, expected_rate, verify_decoding, use_stdio)
    test_suite = [
        ("sine_1k", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), [], 44100, True, False),
        ("silence", lambda f: ffmpeg.input("anullsrc=r=44100:cl=stereo:d=1", f="lavfi").output(f).run(quiet=True), [], 44100, True, False),
        ("noise", lambda f: ffmpeg.input("anoisesrc=d=1:c=white:r=44100", f="lavfi").output(f).run(quiet=True), [], 44100, True, False),
        ("vibrato", lambda f: ffmpeg.input("sine=f=1000:d=1,apulsator=hz=5", f="lavfi").output(f).run(quiet=True), [], 44100, True, False),

        # Sample Rates
        ("rate_8000", lambda f: ffmpeg.input("sine=f=1000:d=1:r=8000", f="lavfi").output(f).run(quiet=True), [], 8000, True, False),
        ("rate_16000", lambda f: ffmpeg.input("sine=f=1000:d=1:r=16000", f="lavfi").output(f).run(quiet=True), [], 16000, True, False),
        ("rate_32000", lambda f: ffmpeg.input("sine=f=1000:d=1:r=32000", f="lavfi").output(f).run(quiet=True), [], 32000, True, False),
        ("rate_48000", lambda f: ffmpeg.input("sine=f=1000:d=1:r=48000", f="lavfi").output(f).run(quiet=True), [], 48000, True, False),

        # Channels
        ("mono", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f, ac=1).run(quiet=True), [], 44100, True, False),

        # Bitrates
        ("bitrate_low", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-b", "32"], 44100, True, False),
        ("bitrate_high", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-b", "256"], 44100, True, False),

        # Quality
        ("quality_low", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-q", "10"], 44100, True, False),
        ("quality_high", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-q", "500"], 44100, True, False),

        # RAW output
        ("raw_output", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-r"], None, False, False),

        # Stdin/Stdout
        ("stdin_stdout", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), [], 44100, True, True),
    ]

    results = {}
    failed = 0
    for test in test_suite:
        name, gen_fn, args, rate, verify, stdio = test
        size = run_test(faac_path, name, gen_fn, args, rate, verify, stdio)
        if size is None:
            failed += 1
        else:
            results[name] = size

    # Bitrate comparison check
    if "bitrate_low" in results and "bitrate_high" in results:
        low = results["bitrate_low"]
        high = results["bitrate_high"]
        if high <= low:
            print(f"Error: High bitrate output ({high} bytes) is not larger than low bitrate ({low} bytes)")
            failed += 1
        else:
            print("Bitrate size ordering verified.")

    if failed > 0:
        print(f"\n{failed} tests failed.")
        sys.exit(1)
    else:
        print("\nAll sanity tests passed.")

if __name__ == "__main__":
    main()
