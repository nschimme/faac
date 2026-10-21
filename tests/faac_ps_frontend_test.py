#!/usr/bin/env python3
"""PS carrier, channel direction, signaling and strict decoder controls."""
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave


def run(cmd):
    result = subprocess.run(cmd, capture_output=True)
    assert result.returncode == 0, result.stderr.decode(errors='replace')


faac, faad = sys.argv[1:3]
with tempfile.TemporaryDirectory() as directory:
    for rate in (32000, 44100, 48000):
        for mode in ('left', 'right', 'antiphase'):
            source = os.path.join(directory, f'{rate}-{mode}.wav')
            with wave.open(source, 'wb') as wav:
                wav.setnchannels(2)
                wav.setsampwidth(2)
                wav.setframerate(rate)
                frames = []
                for i in range(rate):
                    sample = int(8000 * math.sin(2 * math.pi * 1000 * i / rate))
                    pair = (sample, 0) if mode == 'left' else (0, sample) if mode == 'right' else (sample, -sample)
                    frames.append(struct.pack('<hh', *pair))
                wav.writeframes(b''.join(frames))
            for ext in ('m4a', 'aac'):
                encoded = os.path.join(directory, f'{rate}-{mode}.{ext}')
                output = encoded + '.wav'
                cmd = [faac, '--object-type', 'he-aac-v2', '-b', '16', '-o', encoded, source]
                if ext == 'aac':
                    cmd.append('-a')
                run(cmd)
                if ext == 'aac':
                    with open(encoded, 'rb') as stream:
                        header = stream.read(7)
                    channels = ((header[2] & 1) << 2) | (header[3] >> 6)
                    assert channels == 1, 'ADTS must signal the mono AAC core'
                run([faad, '--strict', '-q', '-o', output, encoded])
                with wave.open(output, 'rb') as wav:
                    assert wav.getnchannels() == 2 and wav.getframerate() == rate
                    if ext == 'm4a':
                        assert abs(wav.getnframes() - rate) <= 1, 'gapless duration'
                    pcm = list(struct.iter_unpack('<hh', wav.readframes(wav.getnframes())))
                # Omit filter startup/end effects when measuring the steady tone.
                pcm = pcm[rate // 4:rate // 2]
                left = sum(l*l for l, r in pcm)
                right = sum(r*r for l, r in pcm)
                cross = sum(l*r for l, r in pcm)
                assert left + right > len(pcm) * 100000, 'carrier collapsed'
                if mode == 'antiphase':
                    assert cross / math.sqrt(left * right) < -0.9, 'antiphase image lost'
                elif mode == 'left':
                    assert left > right * 100, 'left image lost'
                else:
                    assert right > left * 100, 'right image lost'
print('PS frontend: ok')
