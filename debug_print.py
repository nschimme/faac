import sys
import subprocess

def run_test(bitrate):
    cmd = ["./build/frontend/faac", "-b", str(bitrate), "speech_test.wav", "-o", "debug.m4a"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    print(proc.stderr)

run_test(40)
