import wave
import struct
import math
import random

def generate_wav(filename, duration_sec, samplerate, channels, gen_func):
    n_samples = int(duration_sec * samplerate)
    with wave.open(filename, 'w') as f:
        f.setnchannels(channels)
        f.setsampwidth(2) # 16-bit
        f.setframerate(samplerate)

        for i in range(n_samples):
            samples = gen_func(i, samplerate, n_samples)
            if channels == 2:
                # Stereo: same signal on both channels
                data = struct.pack('<hh', int(samples[0]), int(samples[1]))
            else:
                data = struct.pack('<h', int(samples[0]))
            f.writeframesraw(data)

def sine_gen(i, samplerate, n_samples):
    freq = 440.0
    val = math.sin(2.0 * math.pi * freq * i / samplerate)
    int_val = int(val * 32767)
    return (int_val, int_val)

def sweep_gen(i, samplerate, n_samples):
    f_start = 20.0
    f_end = 20000.0
    t = i / samplerate
    duration = n_samples / samplerate
    # Logarithmic sweep
    freq = f_start * (f_end / f_start) ** (t / duration)
    # Phase is integral of frequency
    phase = 2.0 * math.pi * f_start * (duration / math.log(f_end / f_start)) * ((f_end / f_start) ** (t / duration) - 1)
    val = math.sin(phase)
    int_val = int(val * 32767)
    return (int_val, int_val)

def noise_gen(i, samplerate, n_samples):
    val_l = random.uniform(-1, 1)
    val_r = random.uniform(-1, 1)
    return (int(val_l * 32767), int(val_r * 32767))

def silence_gen(i, samplerate, n_samples):
    return (0, 0)

if __name__ == "__main__":
    duration = 180 # 3 minutes
    samplerate = 44100
    channels = 2

    print("Generating sine.wav...")
    generate_wav("sine.wav", duration, samplerate, channels, sine_gen)

    print("Generating sweep.wav...")
    generate_wav("sweep.wav", duration, samplerate, channels, sweep_gen)

    random.seed(42)
    print("Generating noise.wav...")
    generate_wav("noise.wav", duration, samplerate, channels, noise_gen)

    print("Generating silence.wav...")
    generate_wav("silence.wav", duration, samplerate, channels, silence_gen)

    print("Done.")
