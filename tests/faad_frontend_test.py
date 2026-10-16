#!/usr/bin/env python3
"""Round-trip the faad frontend through faac: ADTS and MP4 input, WAV to a
file and to stdout.

Usage: faad_frontend_test.py <faac> <faad> <faam> <max-channels> <decoder-sbr> <decoder-drc> <decoder-dab>
"""

import math
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave


def make_wav(path, channels, rate=44100, secs=2, transient_center=False):
    freqs = (440, 660, 880, 1100, 1320, 1540, 1760, 1980)
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(
            struct.pack("<" + "h" * channels,
                        *(28000 if transient_center and c == 2 and i % (rate // 5) == 0 else
                          int(8000 * math.sin(2 * math.pi * freqs[c % len(freqs)] * i / rate))
                          for c in range(channels)))
            for i in range(int(rate * secs))))


def wav_sizes(data):
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE", "not a WAV"
    i = data.index(b"data")
    return struct.unpack("<I", data[4:8])[0], struct.unpack("<I", data[i + 4:i + 8])[0], len(data) - (i + 8)


def main():
    faac, faad, faam = sys.argv[1:4]
    channels = min(2, int(sys.argv[4]))
    sbr = sys.argv[5] == "true"
    with tempfile.TemporaryDirectory() as d:
        src = os.path.join(d, "in.wav")
        make_wav(src, channels)
        for ext in ("aac", "m4a"):
            enc = os.path.join(d, "t." + ext)
            subprocess.run([faac, "-o", enc, src], check=True, capture_output=True)

            out = os.path.join(d, ext + ".wav")
            subprocess.run([faad, "-q", "-o", out, enc], check=True)
            riff, data, actual = wav_sizes(open(out, "rb").read())
            assert data == actual > 0 and riff == data + 72, f"{ext}: bad WAV header {riff}/{data}/{actual}"

            piped = subprocess.run([faad, "-q", "-w", enc], check=True, capture_output=True).stdout
            riff, data, actual = wav_sizes(piped)
            assert actual > 0, f"{ext}: -w produced no PCM"
            assert riff == 0xFFFFFFFF and data == 0xFFFFFFFF, f"{ext}: -w header must declare a streaming length, got {riff}/{data}"
            assert piped[80:] == open(out, "rb").read()[80:], f"{ext}: -w PCM differs from -o PCM"

            # ADTS seeking must preserve synthesis history, including stdin.
            if ext == "aac":
                skipped = 20
                jump = str((skipped + 0.25) * 1024 / 44100)
                expected = piped[80 + skipped * 1024 * channels * 2:]
                for source in (enc, "-"):
                    result = subprocess.run(
                        [faad, "-q", "-w", "--jump", jump, source],
                        input=open(enc, "rb").read() if source == "-" else None,
                        check=True, capture_output=True)
                    assert result.stdout[80:] == expected, "ADTS jump PCM/history"
                result = subprocess.run([faad, "-q", "-w", "--jump", "10", enc],
                                        check=True, capture_output=True)
                assert result.stdout[80:] == b"", "ADTS jump beyond EOF"

            with open(enc, "rb") as stream:
                stdin_pcm = subprocess.run([faad, "-q", "-w", "-"],
                                           input=stream.read(), check=True,
                                           capture_output=True).stdout
            assert stdin_pcm == piped, f"{ext}: stdin changed the decoded WAV"

        # Default names use only the basename's extension. A leading dot
        # alone is part of a hidden filename, and directory dots stay intact.
        dotted = os.path.join(d, "audio.v1")
        os.mkdir(dotted)
        for name in ("track", ".track", ".track.aac", "track.aac"):
            source = os.path.join(dotted, name)
            with open(source, "wb") as f:
                f.write(open(os.path.join(d, "t.aac"), "rb").read())
            stem = name.rsplit(".", 1)[0] if "." in name[1:] else name
            for options, suffix in (([], ".wav"), (["-f", "raw"], ".raw")):
                expected = os.path.join(dotted, stem + suffix)
                subprocess.run([faad, "-q", "--overwrite", *options, source], check=True)
                assert os.path.getsize(expected) > 0, expected
                os.unlink(expected)
                assert os.path.isfile(source), source

        # A partial final payload or header must fail in strict mode, including
        # stdin and both information-only paths. Lenient decoding stays usable.
        complete = open(os.path.join(d, "t.aac"), "rb").read()
        offset = last = 0
        while offset < len(complete):
            last = offset
            offset += ((complete[offset + 3] & 3) << 11 |
                       complete[offset + 4] << 3 | complete[offset + 5] >> 5)
        for tail in (complete[:-20], complete[:last + 3]):
            truncated = os.path.join(d, "truncated.aac")
            with open(truncated, "wb") as f:
                f.write(tail)
            for options in (("-w",), ("-i",), ("--json",)):
                for source in (truncated, "-"):
                    result = subprocess.run([faad, "--strict", *options, source],
                                            input=tail if source == "-" else None,
                                            capture_output=True)
                    assert result.returncode == 1 and b"frame" in result.stderr.lower(), result.stderr
            subprocess.run([faad, "-q", "-w", truncated], check=True, capture_output=True)

        def check_duration(path, expected):
            metadata = json.loads(subprocess.run([faad, "--json", path],
                                                 check=True, capture_output=True).stdout)
            assert abs(metadata["duration_seconds"] - expected) <= 0.0051, metadata
            bitrate = os.path.getsize(path) * 8 / expected / 1000
            assert abs(metadata["audio"]["bitrate_avg_kbps"] - bitrate) <= 0.051, metadata
            text = subprocess.run([faad, "-i", path], check=True, capture_output=True).stdout.decode()
            assert abs(float(re.search(r"\(([0-9.]+) seconds\)", text)[1]) - expected) <= 0.0051, text
            assert abs(float(re.search(r"Bitrate: +([0-9.]+)", text)[1]) - bitrate) <= 0.051, text

        # Wrap the independent 960-sample LC and HE v2 access units in MP4.
        # Keep the original ASC, which signals the DAB frame length.
        def box(kind, payload):
            return struct.pack(">I4s", len(payload) + 8, kind.encode()) + payload

        def u32(*values):
            return struct.pack(">" + "I" * len(values), *values)

        dab_vectors = (("lc_st48", 48000, 960),
                                           ("he2_st48", 48000 if sbr else 24000,
                                            1920 if sbr else 960))
        for name, rate, frame_samples in (dab_vectors if sys.argv[7] == "true" and channels == 2 else ()):
            fixture = os.path.join(os.path.dirname(__file__), "data", "dab", name + ".dab")
            data = open(fixture, "rb").read()
            asc_len = struct.unpack_from("<I", data)[0]
            asc = data[4:4 + asc_len]
            units = []
            pos = 4 + asc_len
            while pos < len(data):
                size = struct.unpack_from("<I", data, pos)[0]
                units.append(data[pos + 4:pos + 4 + size])
                pos += 4 + size
            # Repeat whole sequences so the seek lands on a frame carrying
            # the initial SBR/PS configuration, as a fresh MP4 decoder needs.
            skipped = len(units) * 2
            units *= 4
            def descriptor(tag, payload):
                assert len(payload) < 128
                return bytes((tag, len(payload))) + payload
            esds = box("esds", u32(0) + descriptor(3, b"\x00\x01\x00" +
                       descriptor(4, b"\x40\x15" + bytes(11) + descriptor(5, asc))))
            entry = box("mp4a", bytes(6) + struct.pack(">H", 1) + bytes(8) +
                        struct.pack(">HHHHI", 2, 16, 0, 0, 48000 << 16) + esds)
            prefix = box("ftyp", b"M4A " + u32(0) + b"M4A ")
            mdat = box("mdat", b"".join(units))
            stbl = box("stbl", box("stsd", u32(0, 1) + entry) +
                       box("stsz", u32(0, 0, len(units), *map(len, units))) +
                       box("stsc", u32(0, 1, 1, len(units), 1)) +
                       box("stco", u32(0, 1, len(prefix) + 8)) +
                       box("stts", u32(0, 1, len(units), 960)))
            mdia = box("mdia", box("mdhd", u32(0, 0, 0, 24000, len(units) * 960, 0)) +
                       box("hdlr", u32(0, 0) + b"soun" + bytes(12)) + box("minf", stbl))
            path = os.path.join(d, name + ".m4a")
            with open(path, "wb") as f:
                f.write(prefix + mdat + box("moov", box("trak", mdia)))
            check_duration(path, len(units) * frame_samples / rate)
            # A fractional frame offset distinguishes 960 from 1024 samples.
            jump = str((skipped + 0.25) * frame_samples / rate)
            result = subprocess.run([faad, "-q", "-w", "-g", "-f", "raw",
                                     "--jump", jump, path], check=True, capture_output=True)
            assert len(result.stdout) == (len(units) - skipped) * frame_samples * 2 * 2, (name, len(units), skipped, len(result.stdout), (len(units) - skipped) * frame_samples * 2 * 2)

        from faad_drc_stream import drc, inject
        drc_path = os.path.join(d, 'drc.aac')
        with open(drc_path, 'wb') as f:
            f.write(inject(open(os.path.join(d, 't.aac'), 'rb').read(), drc(ref=124)))
        def drc_pcm(*options):
            return subprocess.run([faad, '-q', '-w', '-f', 'raw', '-b', '32f',
                                   *options, drc_path], check=True, capture_output=True).stdout
        baseline = drc_pcm()
        enabled = sys.argv[6] == 'true'
        assert (drc_pcm('--drc-cut', '127') != baseline) == enabled
        assert (drc_pcm('--drc-target', '92') != baseline) == enabled
        assert drc_pcm('--drc-boost', '127') == baseline
        with open(drc_path, 'wb') as f:
            f.write(inject(open(os.path.join(d, 't.aac'), 'rb').read(), drc(sign=0)))
        assert (drc_pcm('--drc-boost', '127') != baseline) == enabled
        for option, values in (('--drc-cut', ('-1', '128', 'x')),
                               ('--drc-boost', ('-1', '128', 'x')),
                               ('--drc-target', ('1', '39', '128', 'x'))):
            for value in values:
                result = subprocess.run([faad, '-q', '-w', option, value, drc_path], capture_output=True)
                assert result.returncode == 1, (option, value, result.returncode)

        # Track IDs select the matching AAC stream; ADTS ignores selection.
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
            if "raw" in extra:
                return result.stdout
            return result.stdout[80:]

        first_pcm, second_pcm = pcm(adts1), pcm(adts2)
        check_duration(adts1, len(first_pcm) / (2 * channels * 44100))
        check_duration(adts2, len(second_pcm) / (2 * channels * 48000))
        changing = os.path.join(d, "rate_change.aac")
        with open(changing, "wb") as f:
            f.write(open(adts1, "rb").read() + open(adts2, "rb").read())
        check_duration(changing, len(first_pcm) / (2 * channels * 44100) +
                       len(second_pcm) / (2 * channels * 48000))
        assert first_pcm != second_pcm, "track selection fixtures must differ"
        assert pcm(multi, ["--track", "2"]) == second_pcm, "second track PCM"
        assert pcm(multi) == first_pcm, "default first track PCM"
        for track in ("99", "0", "x"):
            result = subprocess.run([faad, "-q", "-w", "--track", track, multi], capture_output=True)
            assert result.returncode == 1 and result.stderr, (track, result.returncode, result.stderr)
        assert pcm(adts2, ["--track", "99"]) == second_pcm, "ADTS ignores --track"

        for source in (adts2, multi):
            extra = ["--track", "2"] if source == multi else []
            metadata = json.loads(subprocess.run([faad, "--json", *extra, source],
                                                 check=True, capture_output=True).stdout)
            assert metadata["audio"]["channels"] == channels
            assert metadata["audio"]["sample_rate_hz"] == 48000

        if int(sys.argv[4]) >= 3:
            # Exercise the three-channel matrix as well as 5.1 in builds that
            # intentionally cap decoder channels below six.
            three_ch_wav = os.path.join(d, "three_ch.wav")
            make_wav(three_ch_wav, 3, rate=44100, secs=1, transient_center=True)
            three_ch_adts = os.path.join(d, "three_ch.aac")
            subprocess.run([faac, "-a", "--object-type", "lc", "-b", "192",
                            "-o", three_ch_adts, three_ch_wav], check=True, capture_output=True)

            def raw_float(path, extra=()):
                return subprocess.run([faad, "-q", "-f", "raw", "-b", "32f", "-w",
                                       *extra, path], check=True, capture_output=True).stdout

            def check_51_downmix(path):
                full_raw = raw_float(path)
                full = struct.unpack("<" + "f" * (len(full_raw) // 4), full_raw)
                stereo_raw = raw_float(path, ("--downmix", "stereo"))
                mono_raw = raw_float(path, ("--downmix", "mono"))
                stereo = struct.unpack("<" + "f" * (len(stereo_raw) // 4), stereo_raw)
                mono = struct.unpack("<" + "f" * (len(mono_raw) // 4), mono_raw)
                assert len(full) % 6 == 0 and len(stereo) == (len(full) // 6) * 2
                assert len(mono) == len(full) // 6
                k = 0.70710678
                gain = 1.0 / (1.0 + k + k)
                max_error = 0.0
                for i in range(len(mono)):
                    base = i * 6
                    left = (full[base] + k * full[base + 2] + k * full[base + 4]) * gain
                    right = (full[base + 1] + k * full[base + 2] + k * full[base + 5]) * gain
                    max_error = max(max_error, abs(stereo[2 * i] - left),
                                    abs(stereo[2 * i + 1] - right),
                                    abs(mono[i] - 0.5 * (left + right)))
                assert max_error < 1e-6, f"5.1 downmix differs from decoded channels: {max_error}"

            def check_71_stereo_downmix(path):
                full_raw = raw_float(path)
                full = struct.unpack("<" + "f" * (len(full_raw) // 4), full_raw)
                stereo_raw = raw_float(path, ("--downmix", "stereo"))
                stereo = struct.unpack("<" + "f" * (len(stereo_raw) // 4), stereo_raw)
                assert len(full) % 8 == 0 and len(stereo) == (len(full) // 8) * 2
                k = 0.70710678
                gain = 1.0 / (1.0 + 3.0 * k)
                max_error = 0.0
                for i in range(len(stereo) // 2):
                    base = i * 8
                    left = (full[base] + k * (full[base + 2] + full[base + 4] + full[base + 6])) * gain
                    right = (full[base + 1] + k * (full[base + 2] + full[base + 5] + full[base + 7])) * gain
                    max_error = max(max_error, abs(stereo[2 * i] - left),
                                    abs(stereo[2 * i + 1] - right))
                assert max_error < 1e-6, f"7.1 downmix differs from decoded channels: {max_error}"

            full_raw = raw_float(three_ch_adts)
            full = struct.unpack("<" + "f" * (len(full_raw) // 4), full_raw)
            stereo_raw = raw_float(three_ch_adts, ("--downmix", "stereo"))
            mono_raw = raw_float(three_ch_adts, ("--downmix", "mono"))
            stereo = struct.unpack("<" + "f" * (len(stereo_raw) // 4), stereo_raw)
            mono = struct.unpack("<" + "f" * (len(mono_raw) // 4), mono_raw)
            assert len(full) % 3 == 0 and len(stereo) == (len(full) // 3) * 2
            assert len(mono) == len(full) // 3
            k = 0.70710678
            gain = 1.0 / (1.0 + k)
            max_error = 0.0
            for i in range(len(mono)):
                base = i * 3
                left = (full[base] + k * full[base + 2]) * gain
                right = (full[base + 1] + k * full[base + 2]) * gain
                max_error = max(max_error, abs(stereo[2 * i] - left),
                                abs(stereo[2 * i + 1] - right),
                                abs(mono[i] - 0.5 * (left + right)))
            assert max_error < 1e-6, f"3-channel downmix differs from decoded channels: {max_error}"

        if int(sys.argv[4]) >= 6:
            # MP4 gapless format-change transition with channel increase near EOF
            six_ch_wav = os.path.join(d, "six_ch.wav")
            make_wav(six_ch_wav, 6, rate=44100, secs=1, transient_center=True)
            six_ch_adts = os.path.join(d, "six_ch.aac")
            subprocess.run([faac, "-a", "--object-type", "lc", "-b", "384",
                            "-o", six_ch_adts, six_ch_wav], check=True, capture_output=True)

            # Compare decoder-side stereo and mono downmixes with the same
            # decoder's full 5.1 float output. This covers pre-IMDCT mixing and
            # any return to channel IMDCTs when the window states diverge.
            check_51_downmix(six_ch_adts)

            if sbr:
                six_he_adts = os.path.join(d, "six_he.aac")
                subprocess.run([faac, "-a", "--object-type", "he-aac-v1", "-b", "384",
                                "-o", six_he_adts, six_ch_wav], check=True, capture_output=True)
                six_he_mp4 = os.path.join(d, "six_he.m4a")
                subprocess.run([faam, "-i", six_he_adts, "--encoder-delay", "0",
                                "--padding-delay", "0", "-o", six_he_mp4],
                               check=True, capture_output=True)
                check_51_downmix(six_he_mp4)

                if int(sys.argv[4]) >= 8:
                    eight_ch_wav = os.path.join(d, "eight_ch.wav")
                    make_wav(eight_ch_wav, 8, rate=44100, secs=1, transient_center=True)
                    eight_he_adts = os.path.join(d, "eight_he.aac")
                    subprocess.run([faac, "-a", "--object-type", "he-aac-v1", "-b", "512",
                                    "-o", eight_he_adts, eight_ch_wav], check=True, capture_output=True)
                    eight_he_mp4 = os.path.join(d, "eight_he.m4a")
                    subprocess.run([faam, "-i", eight_he_adts, "--encoder-delay", "0",
                                    "--padding-delay", "0", "-o", eight_he_mp4],
                                   check=True, capture_output=True)
                    check_71_stereo_downmix(eight_he_mp4)

            # faam skips headers whose channel configuration changes. Keep
            # the initial ADTS configuration while preserving the six-channel
            # raw AAC elements, so those frames reach the MP4 decoder.
            six_ch_frames = bytearray(open(six_ch_adts, "rb").read())
            offset = 0
            while offset < len(six_ch_frames):
                length = ((six_ch_frames[offset + 3] & 3) << 11 |
                          six_ch_frames[offset + 4] << 3 |
                          six_ch_frames[offset + 5] >> 5)
                six_ch_frames[offset + 2] &= 0xFE
                six_ch_frames[offset + 3] = (six_ch_frames[offset + 3] & 0x3F) | (channels << 6)
                offset += length

            concat_adts = os.path.join(d, "concat.aac")
            with open(concat_adts, "wb") as f_out:
                f_out.write(open(adts1, "rb").read())
                f_out.write(six_ch_frames)

            mp4_gapless = os.path.join(d, "gapless_transition.m4a")
            subprocess.run([faam, "-i", concat_adts, "--encoder-delay", "1024", "--padding-delay", "2000", "-o", mp4_gapless], check=True, capture_output=True)
            # A raw AAC decoder rejects elements exceeding the ASC layout.
            # Allow six channels in the ASC; decoded element counts still
            # transition from two to six in the actual frames.
            mp4_bytes = open(mp4_gapless, "rb").read()
            asc_descriptor = bytes((5, 0x80, 0x80, 0x80, 2, 0x12, channels << 3))
            assert mp4_bytes.count(asc_descriptor) == 1
            with open(mp4_gapless, "wb") as f:
                f.write(mp4_bytes.replace(asc_descriptor, asc_descriptor[:-1] + bytes((6 << 3,))))
            metadata = json.loads(subprocess.run([faad, "--json", mp4_gapless],
                                                 check=True, capture_output=True).stdout)
            assert metadata["audio"]["channels"] == 6, "fixture did not reach six-channel frames"

            mp4_gapless_out = os.path.join(d, "gapless_transition.wav")
            subprocess.run([faad, "-q", "-o", mp4_gapless_out, mp4_gapless], check=True)
            riff, data, actual = wav_sizes(open(mp4_gapless_out, "rb").read())
            assert data == actual > 0 and riff == data + 72, "MP4 gapless format transition produced valid WAV"

            mp4_gapless_piped = subprocess.run([faad, "-q", "-w", mp4_gapless], check=True, capture_output=True).stdout
            assert mp4_gapless_piped[80:] == open(mp4_gapless_out, "rb").read()[80:], "MP4 gapless transition -w PCM differs from -o PCM"

            # Compare both output paths to explicit trimming of the untrimmed
            # stream, including a final channel count larger than the header.
            for bits in ("16", "24", "32", "32f"):
                width = 4 if bits == "32f" else int(bits) // 8
                full_pcm = pcm(mp4_gapless, ["-g", "-f", "raw", "-b", bits])
                expected = full_pcm[1024 * channels * width:-2000 * 6 * width]
                actual = pcm(mp4_gapless, ["-f", "raw", "-b", bits])
                assert actual == expected, "streaming format-change padding differs from explicit trim"
                out = os.path.join(d, "transition.raw")
                subprocess.run([faad, "-q", "--overwrite", "-f", "raw", "-b", bits,
                                "-o", out, mp4_gapless], check=True)
                assert open(out, "rb").read() == expected, "file format-change padding differs from explicit trim"

        # Runtime SBR delay uses output samples; preserve gapless track length.
        he = os.path.join(d, "he.m4a")
        subprocess.run([faac, "--object-type", "he-aac-v1", "-b", "64", "-o", he, src],
                       check=True, capture_output=True)
        he_out = os.path.join(d, "he.wav")
        subprocess.run([faad, "-q", "--strict", "-o", he_out, he], check=True, capture_output=True)
        with wave.open(he_out) as w:
            assert w.getframerate() == (44100 if sbr else 22050) and w.getnchannels() == channels
            assert w.getnframes() == (88200 if sbr else 44100), "runtime decoder delay changed gapless length"

        metadata = json.loads(subprocess.run([faad, "--json", he],
                                             check=True, capture_output=True).stdout)
        assert metadata["audio"]["channels"] == channels
        assert metadata["audio"]["sample_rate_hz"] == (44100 if sbr else 22050)
        assert metadata["audio"]["profile"].startswith("HE-AAC v1" if sbr else "AAC-LC")

        he_adts = os.path.join(d, "he.aac")
        subprocess.run([faad, "-q", "-a", he_adts, he], check=True, capture_output=True)
        check_duration(he_adts, len(pcm(he_adts)) /
                       (2 * channels * (44100 if sbr else 22050)))
        # ADTS must preserve decoder delay regardless of the gapless option.
        for bits in ("16", "24", "32", "32f"):
            for fmt in ("wav", "raw"):
                opts = ["-b", bits, "-f", fmt]
                assert pcm(he_adts, opts) == pcm(he_adts, [*opts, "-g"]), "ADTS delay was trimmed"

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
