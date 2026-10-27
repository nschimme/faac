#!/usr/bin/env python3
# FAAR - Freeware Advanced Audio Resampler
# Copyright (C) 2026 Nils Schimmelmann
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

"""End-to-end checks for the FAAR command-line input and output contract."""
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave


def run(faar, *args):
    return subprocess.run([faar, *map(str, args)], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)


def make_wav(path, frames=257, rate=96000, channels=1):
    with wave.open(path, "wb") as wav:
        wav.setparams((channels, 2, rate, 0, "NONE", ""))
        data = bytearray()
        for frame in range(frames):
            value = round(12000 * math.sin(2 * math.pi * 997 * frame / rate))
            data.extend(struct.pack("<h", value) * channels)
        wav.writeframes(data)


def main():
    faar = os.path.abspath(sys.argv[1])
    max_channels = int(sys.argv[2])
    short_help = run(faar, "-h")
    assert short_help.returncode == 0, short_help.stderr.decode(errors="replace")
    assert b"Usage: faar [options] <input> [output]" in short_help.stdout
    assert b"FAAR 3.0.0" in short_help.stdout
    assert b"-r, --rate" in short_help.stdout
    assert b"--in-rate" in short_help.stdout and b"--allow-extreme-rates" in short_help.stdout
    assert b"--quiet" in short_help.stdout and b"--license" in short_help.stdout
    assert b"--raw" in short_help.stdout and b"-H" in short_help.stdout
    assert b"--help                               Resampler options" in short_help.stdout
    assert b"subject to the configured build limit" not in short_help.stdout

    alias_help = run(faar, "--help")
    assert alias_help.returncode == 0
    assert alias_help.stdout == short_help.stdout

    long_help = run(faar, "-H")
    assert long_help.returncode == 0, long_help.stderr.decode(errors="replace")
    long_text = b" ".join(long_help.stdout.split())
    assert b"subject to the configured build limit" in long_text
    assert b"Use -H for detailed descriptions" in long_text
    assert b"--decimate[=<factor>]" in long_text
    assert b"Float builds support High with compensated float arithmetic" in long_text
    assert b"filter coefficients and FFT twiddles are still prepared with the existing double-precision calculations" in long_text
    assert b"narrower passband" in long_text
    assert b"weaker alias rejection" in long_text
    assert b"greater CPU and setup-memory cost" in long_text

    for args, message in [
        (("--rate", "48000", "--quality", "best", "input.raw", "output.raw"), b"Invalid quality preset"),
        (("--rate", "48000", "--format", "s32le", "input.raw", "output.raw"), b"Invalid raw PCM format"),
        (("--rate", "48000", "-v", "3", "input.raw", "output.raw"), b"Invalid verbosity"),
        (("--rate", "48000", "input.raw", "output.raw", "extra"), b"Unexpected argument"),
    ]:
        invalid_option = run(faar, *args)
        assert invalid_option.returncode != 0
        assert message in invalid_option.stderr

    license_result = run(faar, "--license")
    assert license_result.returncode == 0
    assert b"Lesser General Public License" in license_result.stdout

    with tempfile.TemporaryDirectory(prefix="faar-cli-") as directory:
        source = os.path.join(directory, "source.wav")
        output = os.path.join(directory, "converted.wav")
        make_wav(source)

        converted = run(faar, "--rate", "44100", "--quality", "good",
                        "--format", "s16le", "--dither", "tpdf", "-v", "2", source, output)
        assert converted.returncode == 0, converted.stderr.decode(errors="replace")
        assert b"Freeware Advanced Audio Resampler" in converted.stderr
        assert b"100%" in converted.stderr
        assert b"Completed:" in converted.stderr
        with wave.open(output, "rb") as wav:
            assert wav.getframerate() == 44100
            assert wav.getnchannels() == 1
            assert wav.getsampwidth() == 2
            assert wav.getnframes() == (257 * 44100 + 96000 - 1) // 96000

        # An output directory converts every input there; inputs sharing a
        # rate and channel count share one filter design, with the same result
        # as converting each file alone.
        second = os.path.join(directory, "second.wav")
        third = os.path.join(directory, "third.wav")
        make_wav(second, frames=300)
        make_wav(third, frames=199, rate=48000, channels=min(2, max_channels))
        batch_dir = os.path.join(directory, "batch")
        os.mkdir(batch_dir)
        batch = run(faar, "--rate", "44100", "-q", "-o", batch_dir, source, second, third)
        assert batch.returncode == 0, batch.stderr.decode(errors="replace")
        for name, path in (("source.wav", source), ("second.wav", second), ("third.wav", third)):
            alone = os.path.join(directory, "alone-" + name)
            single = run(faar, "--rate", "44100", "-q", path, alone)
            assert single.returncode == 0, single.stderr.decode(errors="replace")
            assert open(alone, "rb").read() == open(os.path.join(batch_dir, name), "rb").read()
        again = run(faar, "--rate", "44100", "-q", "-o", batch_dir, source)
        assert again.returncode != 0 and b"already exists" in again.stderr
        assert run(faar, "--rate", "44100", "-q", "-o", batch_dir, "-").returncode != 0

        # Raw float input/output stays headerless and uses little-endian samples.
        raw_in = os.path.join(directory, "source.f32")
        raw_out = os.path.join(directory, "converted.f32")
        raw_samples = [0.5 * math.sin(2 * math.pi * 997 * n / 96000) for n in range(257)]
        with open(raw_in, "wb") as stream:
            stream.write(struct.pack("<257f", *raw_samples))
        raw_data = open(raw_in, "rb").read()
        raw_result = run(faar, "--rate", "48000", "--in-rate", "96000",
                         "--channels", "1", "--format", "f32le", "--output",
                         raw_out, raw_in)
        assert raw_result.returncode == 0, raw_result.stderr.decode(errors="replace")
        data = open(raw_out, "rb").read()
        assert len(data) == 129 * 4

        # A .wav suffix selects WAV output for raw input, case-insensitively.
        raw_wav = os.path.join(directory, "raw-converted.WAV")
        raw_wav_result = run(faar, "--raw", "--rate", "48000", "--in-rate", "96000",
                             "--channels", "1", "--format", "f32le", raw_in, raw_wav)
        assert raw_wav_result.returncode == 0, raw_wav_result.stderr.decode(errors="replace")
        wav_data = open(raw_wav, "rb").read()
        assert wav_data[:4] == b"RIFF" and wav_data[8:12] == b"WAVE"
        wav_fmt = struct.unpack_from("<HHIIHH", wav_data, 56)
        assert wav_fmt == (3, 1, 48000, 48000 * 4, 4, 32)
        assert wav_data[72:76] == b"data"
        assert struct.unpack_from("<I", wav_data, 76)[0] == 129 * 4

        # The extension must be a suffix; an incidental .wav in the middle is raw PCM.
        raw_with_suffix = os.path.join(directory, "converted.wav.raw")
        suffix_result = run(faar, "--raw", "--rate", "48000", "--in-rate", "96000",
                            "--channels", "1", "--format", "f32le", raw_in, raw_with_suffix)
        assert suffix_result.returncode == 0, suffix_result.stderr.decode(errors="replace")
        assert len(open(raw_with_suffix, "rb").read()) == 129 * 4

        # A stdout pipeline contains only PCM; the shared progress display stays on stderr.
        streamed = subprocess.run(
            [faar, "--raw", "--rate", "48000", "--in-rate", "96000", "--channels", "1",
             "--format", "f32le", "-", "-"],
            input=raw_data, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert streamed.returncode == 0, streamed.stderr.decode(errors="replace")
        assert len(streamed.stdout) == 129 * 4
        assert b"Resampling input" in streamed.stderr
        assert b"Freeware Advanced Audio Resampler" not in streamed.stdout

        quiet_out = os.path.join(directory, "quiet.f32")
        quiet_result = run(faar, "--rate", "48000", "--in-rate", "96000",
                           "--channels", "1", "--format", "f32le", "--quiet",
                           "--output", quiet_out, raw_in)
        assert quiet_result.returncode == 0, quiet_result.stderr.decode(errors="replace")
        assert quiet_result.stderr == b""

        # Refuse aliases before opening the destination, preserving the input.
        original = open(source, "rb").read()
        same_path = run(faar, "--rate", "44100", source, source)
        assert same_path.returncode != 0
        assert open(source, "rb").read() == original

        invalid = run(faar, "--rate", "1000", source, output)
        assert invalid.returncode != 0
        assert b"Invalid target sample rate" in invalid.stderr


if __name__ == "__main__":
    main()
