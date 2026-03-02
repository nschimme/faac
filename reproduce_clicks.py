import wave
import struct
import math

def generate_loud_clicks(filename, duration=5, sample_rate=16000):
    num_samples = int(duration * sample_rate)
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        for i in range(num_samples):
            # Normal quiet background
            value = int(500 * math.sin(2.0 * math.pi * 440 * i / sample_rate))

            # Loud bursts every second
            if (i % sample_rate) < (sample_rate // 10):
                # Very loud 1000Hz tone
                value = int(30000 * math.sin(2.0 * math.pi * 1000 * i / sample_rate))

            wav_file.writeframes(struct.pack('<h', max(-32768, min(32767, int(value)))))

if __name__ == "__main__":
    generate_loud_clicks('loud_clicks.wav')
