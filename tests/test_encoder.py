import ffmpeg
import subprocess
import sys
import os
import tempfile

def run_test(faac_path, name, gen_fn, faac_args):
    print(f"--- Running Test: {name} ---")
    with tempfile.TemporaryDirectory() as out_dir:
        wav_file = os.path.join(out_dir, "input.wav")
        aac_file = os.path.join(out_dir, "output.aac")
        dec_file = os.path.join(out_dir, "decoded.wav")

        try:
            gen_fn(wav_file)

            cmd = [faac_path, "-o", aac_file] + faac_args + [wav_file]
            print(f"Encoding: {' '.join(cmd)}")
            subprocess.run(cmd, check=True, capture_output=True)

            if not os.path.exists(aac_file) or os.path.getsize(aac_file) == 0:
                print(f"Error: Output AAC file is empty or not created.")
                return False

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
                return False

            print(f"Test {name} passed.")
            return True

        except ffmpeg.Error as e:
            print(f"FFmpeg error: {e.stderr.decode() if e.stderr else str(e)}")
            return False
        except subprocess.CalledProcessError as e:
            print(f"FAAC error: {e.stderr.decode() if e.stderr else str(e)}")
            return False

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path_to_faac>")
        sys.exit(1)

    faac_path = sys.argv[1]

    # Check if ffmpeg is installed
    try:
        subprocess.run(["ffmpeg", "-version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("FFmpeg not found. Skipping tests.")
        sys.exit(0)

    tests = []

    # Basic signal types
    tests.append(("sine_1k", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), []))
    tests.append(("silence", lambda f: ffmpeg.input("anullsrc=r=44100:cl=stereo:d=1", f="lavfi").output(f).run(quiet=True), []))
    tests.append(("noise", lambda f: ffmpeg.input("anoisesrc=d=1:c=white:r=44100", f="lavfi").output(f).run(quiet=True), []))
    tests.append(("vibrato", lambda f: ffmpeg.input("sine=f=1000:d=1,apulsator=hz=5", f="lavfi").output(f).run(quiet=True), []))

    # Sample Rates
    for rate in [8000, 16000, 32000, 48000]:
        tests.append((f"rate_{rate}", lambda f, r=rate: ffmpeg.input(f"sine=f=1000:d=1:r={r}", f="lavfi").output(f).run(quiet=True), []))

    # Channels
    tests.append(("mono", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f, ac=1).run(quiet=True), []))

    # Bitrates
    tests.append(("bitrate_low", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-b", "32"]))
    tests.append(("bitrate_high", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-b", "256"]))

    # Quality
    tests.append(("quality_low", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-q", "10"]))
    tests.append(("quality_high", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-q", "500"]))

    # RAW output (skip decoding verification for raw as it's harder to probe without extra info)
    tests.append(("raw_output", lambda f: ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(f).run(quiet=True), ["-r"]))

    def run_raw_test(faac_path, name, gen_fn, faac_args):
        print(f"--- Running Test: {name} ---")
        with tempfile.TemporaryDirectory() as out_dir:
            wav_file = os.path.join(out_dir, "input.wav")
            aac_file = os.path.join(out_dir, "output.aac")
            gen_fn(wav_file)
            cmd = [faac_path, "-o", aac_file] + faac_args + [wav_file]
            print(f"Encoding: {' '.join(cmd)}")
            subprocess.run(cmd, check=True, capture_output=True)
            if os.path.exists(aac_file) and os.path.getsize(aac_file) > 0:
                print(f"Test {name} passed.")
                return True
            return False

    # Bitrate and Quality tests are already appended.
    # Let's handle raw separately to avoid the decoding failure.

    # Test Stdin/Stdout
    print("--- Running Test: stdin_stdout ---")
    with tempfile.TemporaryDirectory() as out_dir:
        wav_file = os.path.join(out_dir, "input.wav")
        aac_file = os.path.join(out_dir, "output.aac")
        ffmpeg.input("sine=f=1000:d=1", f="lavfi").output(wav_file).run(quiet=True)
        try:
            with open(wav_file, "rb") as fin, open(aac_file, "wb") as fout:
                subprocess.run([faac_path, "-o", "-", "-"], stdin=fin, stdout=fout, check=True)
            if os.path.getsize(aac_file) > 0:
                print("Test stdin_stdout passed.")
                tests.append(("stdin_stdout_result", lambda f: None, [])) # dummy for count
            else:
                print("Error: stdin_stdout produced empty output.")
                sys.exit(1)
        except Exception as e:
            print(f"stdin_stdout failed: {e}")
            sys.exit(1)

    failed = 0
    for name, gen_fn, args in tests:
        if name == "stdin_stdout_result": continue
        if name == "raw_output":
            if not run_raw_test(faac_path, name, gen_fn, args):
                failed += 1
            continue
        if not run_test(faac_path, name, gen_fn, args):
            failed += 1

    if failed > 0:
        print(f"\n{failed} tests failed.")
        sys.exit(1)
    else:
        print("\nAll sanity tests passed.")

if __name__ == "__main__":
    main()
