import wave, struct, math
with wave.open('test.wav', 'w') as f:
    f.setnchannels(1); f.setsampwidth(2); f.setframerate(44100)
    for i in range(44100):
        val = int(32767.0 * math.sin(2.0 * math.pi * 440.0 * i / 44100.0))
        f.writeframes(struct.pack('<h', val))
