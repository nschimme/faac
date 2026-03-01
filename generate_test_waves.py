import wave
import math
import struct
import random
import os

def generate_wave(filename, duration_sec, type='sine', sample_rate=44100):
    n_samples = duration_sec * sample_rate
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(2)  # Stereo
        wav_file.setsampwidth(2)  # 16-bit
        wav_file.setframerate(sample_rate)

        if type == 'noise':
            random.seed(42)

        for i in range(n_samples):
            if type == 'sine':
                value = math.sin(2.0 * math.pi * 440.0 * i / sample_rate)
            elif type == 'sweep':
                freq = 20.0 + (20000.0 - 20.0) * (i / n_samples)
                value = math.sin(2.0 * math.pi * freq * i / sample_rate)
            elif type == 'noise':
                value = random.uniform(-1.0, 1.0)
            elif type == 'silence':
                value = 0.0
            else:
                value = 0.0

            int_val = int(value * 32767.0)
            packed_val = struct.pack('<h', int_val)
            wav_file.writeframesraw(packed_val) # Left
            wav_file.writeframesraw(packed_val) # Right

if __name__ == "__main__":
    duration = 600 # 10 minutes
    os.makedirs('test_waves', exist_ok=True)
    print("Generating sine.wav...")
    generate_wave('test_waves/sine.wav', duration, 'sine')
    print("Generating sweep.wav...")
    generate_wave('test_waves/sweep.wav', duration, 'sweep')
    print("Generating noise.wav...")
    generate_wave('test_waves/noise.wav', duration, 'noise')
    print("Generating silence.wav...")
    generate_wave('test_waves/silence.wav', duration, 'silence')
    print("Done.")
