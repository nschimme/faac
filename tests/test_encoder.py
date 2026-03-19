import ffmpeg
import subprocess
import sys
import os
import tempfile

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <path_to_faac> <test_type>")
        sys.exit(1)

    faac_path = sys.argv[1]
    test_type = sys.argv[2]

    # Check if ffmpeg is installed
    try:
        subprocess.run(["ffmpeg", "-version"], check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("FFmpeg not found. Skipping test.")
        sys.exit(0)

    with tempfile.TemporaryDirectory() as out_dir:
        wav_file = os.path.join(out_dir, f"test_{test_type}.wav")
        aac_file = os.path.join(out_dir, f"test_{test_type}.aac")
        dec_file = os.path.join(out_dir, f"test_{test_type}_dec.wav")

        try:
            if test_type == "sine":
                (
                    ffmpeg
                    .input("sine=frequency=1000:duration=2", f="lavfi")
                    .output(wav_file)
                    .overwrite_output()
                    .run(quiet=True)
                )
            elif test_type == "sweep":
                # Vibrato/Pulsating sine as a proxy for sweep since 't' is not supported in 'sine' frequency
                (
                    ffmpeg
                    .input("sine=f=1000:d=2,apulsator=hz=5", f="lavfi")
                    .output(wav_file)
                    .overwrite_output()
                    .run(quiet=True)
                )
            elif test_type == "silence":
                (
                    ffmpeg
                    .input("anullsrc=r=44100:cl=stereo:d=2", f="lavfi")
                    .output(wav_file)
                    .overwrite_output()
                    .run(quiet=True)
                )
            elif test_type == "noise":
                (
                    ffmpeg
                    .input("anoisesrc=d=2:c=white:r=44100", f="lavfi")
                    .output(wav_file)
                    .overwrite_output()
                    .run(quiet=True)
                )
            else:
                print(f"Unknown test type: {test_type}")
                sys.exit(1)

            print(f"Encoding {test_type} with {faac_path}...")
            subprocess.run([faac_path, "-o", aac_file, wav_file], check=True, capture_output=True)

            if not os.path.exists(aac_file) or os.path.getsize(aac_file) == 0:
                print(f"Error: Output AAC file {aac_file} is empty or not created.")
                sys.exit(1)

            print(f"Decoding {test_type} back with ffmpeg...")
            (
                ffmpeg
                .input(aac_file)
                .output(dec_file)
                .overwrite_output()
                .run(quiet=True)
            )

            if not os.path.exists(dec_file) or os.path.getsize(dec_file) == 0:
                print(f"Error: Decoded WAV file {dec_file} is empty or not created.")
                sys.exit(1)

            print(f"Test {test_type} passed successfully.")

        except ffmpeg.Error as e:
            print(f"FFmpeg error: {e.stderr.decode() if e.stderr else str(e)}")
            sys.exit(1)
        except subprocess.CalledProcessError as e:
            print(f"FAAC error: {e.stderr.decode() if e.stderr else str(e)}")
            sys.exit(1)

if __name__ == "__main__":
    main()
