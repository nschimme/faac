#!/usr/bin/env python3
"""Round-trip the faad frontend through faac: ADTS and MP4 input, WAV to a
file and to stdout.

Usage: faad_frontend_test.py <faac> <faad> <max-channels> <decoder-sbr>
"""

import math
import os
import struct
import subprocess
import sys
import tempfile
import wave


def make_wav(path, channels, rate=44100, secs=2):
    freqs = (440, 660, 880, 1100, 1320, 1540, 1760, 1980)
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(
            struct.pack("<" + "h" * channels,
                        *(int(8000 * math.sin(2 * math.pi * freqs[c % len(freqs)] * i / rate))
                          for c in range(channels)))
            for i in range(int(rate * secs))))


def wav_sizes(data):
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE", "not a WAV"
    i = data.index(b"data")
    return struct.unpack("<I", data[4:8])[0], struct.unpack("<I", data[i + 4:i + 8])[0], len(data) - (i + 8)


def main():
    faac, faad = sys.argv[1:3]
    channels = min(2, int(sys.argv[3]))
    sbr = sys.argv[4] == "true"
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "in.wav")
        make_wav(src, channels)
        for ext in ("aac", "m4a"):
            enc = os.path.join(d, "t." + ext)
            subprocess.run([faac, "-o", enc, src], check=True, capture_output=True)

            out = os.path.join(d, ext + ".wav")
            subprocess.run([faad, "-q", "-o", out, enc], check=True)
            riff, data, actual = wav_sizes(open(out, "rb").read())
            assert data == actual > 0 and riff == data + 36, f"{ext}: bad WAV header {riff}/{data}/{actual}"

            piped = subprocess.run([faad, "-q", "-w", enc], check=True, capture_output=True).stdout
            riff, data, actual = wav_sizes(piped)
            assert actual > 0, f"{ext}: -w produced no PCM"
            assert riff == 0xFFFFFFFF and data == 0xFFFFFFFF, f"{ext}: -w header must declare a streaming length, got {riff}/{data}"
            assert piped[44:] == open(out, "rb").read()[44:], f"{ext}: -w PCM differs from -o PCM"

            with open(enc, "rb") as stream:
                stdin_pcm = subprocess.run([faad, "-q", "-w", "-"],
                                           input=stream.read(), check=True,
                                           capture_output=True).stdout
            assert stdin_pcm == piped, f"{ext}: stdin changed the decoded WAV"

        # Track IDs select the matching AAC stream; ADTS ignores selection.
        faam = os.path.join(os.path.dirname(os.path.abspath(faac)), "faam")
        second = os.path.join(d, "second.wav")
        make_wav(second, channels, rate=48000, secs=1)
        adts2 = os.path.join(d, "second.aac")
        subprocess.run([faac, "-a", "-o", adts2, second], check=True, capture_output=True)
        adts1 = os.path.join(d, "t.aac")
        multi = os.path.join(d, "multi.m4a")
        subprocess.run([faam, "-i", adts1, "-i", adts2, "-o", multi], check=True, capture_output=True)
        info = subprocess.run([faad, "-i", multi], check=True, capture_output=True)
        listing = info.stdout + info.stderr
        assert b"Tracks:" in listing and b"Track 1" in listing and b"Track 2" in listing, listing

        def pcm(path, extra=()):
            result = subprocess.run([faad, "-q", "-w"] + list(extra) + [path], check=True, capture_output=True)
            return result.stdout[44:]

        first_pcm, second_pcm = pcm(adts1), pcm(adts2)
        assert first_pcm != second_pcm, "track selection fixtures must differ"
        assert pcm(multi, ["--track", "2"]) == second_pcm, "second track PCM"
        assert pcm(multi) == first_pcm, "default first track PCM"
        for track in ("99", "0", "x"):
            result = subprocess.run([faad, "-q", "-w", "--track", track, multi], capture_output=True)
            assert result.returncode == 1 and result.stderr, (track, result.returncode, result.stderr)
        assert pcm(adts2, ["--track", "99"]) == second_pcm, "ADTS ignores --track"

        # MP4 gapless format-change transition with channel increase near EOF
        six_ch_wav = os.path.join(d, "six_ch.wav")
        make_wav(six_ch_wav, 6, rate=44100, secs=0.1)
        six_ch_adts = os.path.join(d, "six_ch.aac")
        subprocess.run([faac, "-a", "-o", six_ch_adts, six_ch_wav], check=True, capture_output=True)

        concat_adts = os.path.join(d, "concat.aac")
        with open(concat_adts, "wb") as f_out:
            f_out.write(open(adts1, "rb").read())
            f_out.write(open(six_ch_adts, "rb").read())

        mp4_gapless = os.path.join(d, "gapless_transition.m4a")
        subprocess.run([faam, "-i", concat_adts, "--encoder-delay", "1024", "--padding-delay", "2000", "-o", mp4_gapless], check=True, capture_output=True)

        mp4_gapless_out = os.path.join(d, "gapless_transition.wav")
        subprocess.run([faad, "-q", "-o", mp4_gapless_out, mp4_gapless], check=True)
        riff, data, actual = wav_sizes(open(mp4_gapless_out, "rb").read())
        assert data == actual > 0 and riff == data + 36, "MP4 gapless format transition produced valid WAV"

        mp4_gapless_piped = subprocess.run([faad, "-q", "-w", mp4_gapless], check=True, capture_output=True).stdout
        assert mp4_gapless_piped[44:] == open(mp4_gapless_out, "rb").read()[44:], "MP4 gapless transition -w PCM differs from -o PCM"

        # Runtime SBR delay uses output samples; preserve gapless track length.
        he = os.path.join(d, "he.m4a")
        subprocess.run([faac, "--object-type", "he-aac-v1", "-b", "64", "-o", he, src],
                       check=True, capture_output=True)
        he_out = os.path.join(d, "he.wav")
        subprocess.run([faad, "-q", "--strict", "-o", he_out, he], check=True, capture_output=True)
        with wave.open(he_out) as w:
            assert w.getframerate() == (44100 if sbr else 22050) and w.getnchannels() == channels
            assert w.getnframes() == (88200 if sbr else 44100), "runtime decoder delay changed gapless length"

        # Concealed core and damaged-SBR recovery both produce usable audio
        # normally, but strict mode must reject them.
        for name, payload in (("concealed", b"\xe0"),
                              ("degraded", bytes.fromhex("00c800063dfc000e"))):
            length = 7 + len(payload)
            header = bytes((0xff, 0xf1, 0x50, 0x40 | (length >> 11),
                            (length >> 3) & 0xff, ((length & 7) << 5) | 0x1f, 0xfc))
            damaged = os.path.join(d, name + ".aac")
            with open(damaged, "wb") as f:
                f.write(header + payload)
            out = os.path.join(d, name + ".wav")
            subprocess.run([faad, "-q", "-o", out, damaged], check=True, capture_output=True)
            assert wav_sizes(open(out, "rb").read())[2] > 0
            result = subprocess.run([faad, "-q", "--strict", "--overwrite", "-o", out, damaged],
                                    capture_output=True)
            if name == "concealed" or sbr:
                assert result.returncode == 1 and b"frame" in result.stderr.lower(), name + " strict mode"
            else:
                assert result.returncode == 0, "disabled SBR leaves the intact core usable"
    print("faad frontend: ok")


if __name__ == "__main__":
    main()
