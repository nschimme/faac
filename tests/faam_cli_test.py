#!/usr/bin/env python3
"""Regression test for the faam CLI: mux correctness, strict values, overwrite
protection, metadata at mux time and in tag, chapters, gapless length.

Usage: faam_cli_test.py <faac> <faam> <faad>

Needs a faam built with -Dmuxer=true; the video checks run when the build also
has -Dmuxer-video=true and are skipped otherwise. The >4 GiB checks use sparse files and are
skipped where there are none; FAAM_TEST_BIG=1 adds the moov-first case, which writes about 5 GB.
"""

import hashlib
import math
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

failures = []
checks = 0


def check(cond, what):
    global checks
    checks += 1
    if not cond:
        failures.append(what)
        print("FAIL:", what)


def make_wav(path, rate=48000, secs=5, channels=2, sine=False):
    rnd = random.Random(1)
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for i in range(int(rate * secs)):
            t = i / rate
            left = 8000 * math.sin(2 * math.pi * 440 * t) + 3000 * math.sin(2 * math.pi * 1234 * t) + rnd.randint(-800, 800)
            right = 8000 * math.sin(2 * math.pi * 660 * t) + 3000 * math.sin(2 * math.pi * 2345 * t) + rnd.randint(-800, 800)
            if sine:
                left = 8000 * math.sin(2 * math.pi * 440 * t)
                right = 8000 * math.sin(2 * math.pi * 660 * t)
            frames += struct.pack("<hh", int(left), int(right)) if channels == 2 else struct.pack("<h", int(left))
        w.writeframes(bytes(frames))


class Tools:
    def __init__(self, faac, faam, faad):
        self.faac, self.faam, self.faad = faac, faam, faad

    def run(self, exe, args, cwd, env=None):
        e = dict(os.environ)
        e.pop("SOURCE_DATE_EPOCH", None)
        if env:
            e.update(env)
        r = subprocess.run([exe] + list(args), cwd=cwd, env=e, capture_output=True)
        # a sanitizer build reports through stderr; an expected-failure exit code would hide it
        if re.search(rb"AddressSanitizer|runtime error:|LeakSanitizer", r.stderr):
            check(False, f"sanitizer report from {os.path.basename(exe)} {' '.join(args)}")
        return r

    def faam_run(self, args, cwd, env=None):
        return self.run(self.faam, args, cwd, env)


def boxes(buf, start=0, end=None):
    end = len(buf) if end is None else end
    pos = start
    while pos + 8 <= end:
        size, kind = struct.unpack_from(">I4s", buf, pos)
        hdr = 8
        if size == 1:
            size = struct.unpack_from(">Q", buf, pos + 8)[0]
            hdr = 16
        if size < hdr or pos + size > end:
            return
        yield kind, pos + hdr, pos + size
        pos += size


def find(buf, path, start=0, end=None):
    """Payload range of the box at path, e.g. ("moov", "trak", "mdia", "mdhd")."""
    for kind, a, b in boxes(buf, start, end):
        if kind.decode("latin1") == path[0]:
            if len(path) == 1:
                return a, b
            if path[0] == "meta":
                a += 4
            sub = find(buf, path[1:], a, b)
            if sub:
                return sub
    return None


def elst(buf):
    r = find(buf, ("moov", "trak", "edts", "elst"))
    if not r:
        return None
    return struct.unpack_from(">II", buf, r[0] + 8)  # seg_dur, media_time of entry 1


def smpb(buf):
    m = re.search(rb" 0{8} ([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{16})", buf)
    return tuple(int(x, 16) for x in m.groups()) if m else None


def mdhd_lang(buf):
    r = find(buf, ("moov", "trak", "mdia", "mdhd"))
    return struct.unpack_from(">H", buf, r[0] + 20)[0]


def mvhd_ctime(buf):
    r = find(buf, ("moov", "mvhd"))
    return struct.unpack_from(">I", buf, r[0] + 4)[0]


def mvhd_ctime_any(buf):
    r = find(buf, ("moov", "mvhd"))
    if buf[r[0]] == 1:
        return struct.unpack_from(">Q", buf, r[0] + 4)[0]
    return struct.unpack_from(">I", buf, r[0] + 4)[0]


def stts(buf):
    r = find(buf, ("moov", "trak", "mdia", "minf", "stbl", "stts"))
    n = struct.unpack_from(">I", buf, r[0] + 4)[0]
    return [struct.unpack_from(">II", buf, r[0] + 8 + 8 * i) for i in range(n)]


def read(path):
    with open(path, "rb") as f:
        return f.read()


def wav_data_len(path):
    d = read(path)
    return len(d) - (d.index(b"data") + 8)


def info_tags(out):
    """The Metadata Tags block of `faam info`."""
    text = out.decode()
    if "Metadata Tags:" not in text:
        return ""
    part = text.split("Metadata Tags:")[1]
    return part.split("\n\n")[0]


def main():
    tools = Tools(*[os.path.abspath(p) for p in sys.argv[1:4]])
    with tempfile.TemporaryDirectory() as d:
        run_all(tools, d)
    print(f"{checks - len(failures)}/{checks} checks passed")
    if failures:
        sys.exit(1)


def no_output(d, name):
    return not os.path.exists(os.path.join(d, name))


def test_gapless_boxes(t, d):
    make_wav(os.path.join(d, "gapless.wav"), rate=44100, secs=3.1, sine=True)
    for name, extra, sbr in (("gapless_lc", ["-q", "100"], []),
                             ("gapless_he", ["--object-type", "he-aac-v1", "-b", "48"],
                              ["--sbr-signaling", "compatible"])):
        for ext in ("m4a", "aac"):
            r = t.run(t.faac, ["-v0"] + extra + ["-o", f"{name}.{ext}", "gapless.wav"], d)
            check(r.returncode == 0, f"{name}.{ext}: encode")
            if r.returncode:
                return
        r = t.faam_run(["info", f"{name}.m4a"], d)
        delay = re.search(rb"Encoder Delay: (\d+) samples", r.stdout)
        padding = re.search(rb"Trailing Padding: (\d+) samples", r.stdout)
        check(r.returncode == 0 and delay is not None and padding is not None, f"{name}: info gapless values")
        if r.returncode or delay is None or padding is None:
            continue
        r = t.faam_run(["-i", f"{name}.aac", "-o", f"{name}_mux.m4a",
                        "--encoder-delay", delay[1].decode(), "--padding-delay", padding[1].decode()] + sbr, d)
        check(r.returncode == 0, f"{name}: remux")
        if r.returncode:
            continue
        ref = read(os.path.join(d, f"{name}.m4a"))
        mine = read(os.path.join(d, f"{name}_mux.m4a"))
        for path in (("moov", "trak", "edts", "elst"),
                     ("moov", "trak", "mdia", "mdhd"),
                     ("moov", "trak", "mdia", "minf", "stbl", "stts")):
            a, b = find(ref, path), find(mine, path)
            check(a is not None and b is not None, f"{name}: {path[-1]} present")
            if a is None or b is None:
                continue
            header = 8 if path[-1] == "elst" else 0
            x, y = ref[a[0] - header:a[1]], mine[b[0] - header:b[1]]
            if path[-1] == "mdhd":
                # Creation and modification times do not describe the audio timeline.
                times_end = 20 if x[0] == 1 else 12
                x = x[:4] + bytes(times_end - 4) + x[times_end:]
                times_end = 20 if y[0] == 1 else 12
                y = y[:4] + bytes(times_end - 4) + y[times_end:]
            check(x == y, f"{name}: {path[-1]} payload byte identity")
        payloads = []
        for buf in (ref, mine):
            r = find(buf, ("moov", "udta", "meta", "ilst"))
            matches = [] if r is None else [buf[a:b] for kind, a, b in boxes(buf, *r)
                                            if kind == b"----" and b"iTunSMPB" in buf[a:b]]
            check(len(matches) == 1, f"{name}: one iTunSMPB atom")
            payloads.append(matches)
        check(payloads[0] == payloads[1] and bool(payloads[0]), f"{name}: iTunSMPB atom payload byte identity")


def test_abi_track_count(t, d):
    # The moov follows mdat, so replacing trak boxes cannot move sample bytes.
    src = read(os.path.join(d, "plain.m4a"))
    a, b = find(src, ("moov",))
    children = list(boxes(src, a, b))
    track = next(src[x - 8:y] for kind, x, y in children if kind == b"trak")
    copies = []
    for tid in range(21, 33):
        copy = bytearray(track)
        x, y = find(copy, ("trak", "tkhd"))
        struct.pack_into(">I", copy, x + (20 if copy[x] == 1 else 12), tid)
        copies.append(copy)
    body = b"".join(src[x - 8:y] for kind, x, y in children if kind != b"trak") + b"".join(copies)
    data = src[:a - 8] + struct.pack(">I4s", len(body) + 8, b"moov") + body + src[b:]
    open(os.path.join(d, "twelve.m4a"), "wb").write(data)
    r = t.faam_run(["info", "twelve.m4a"], d)
    check(r.returncode == 0 and b"Tracks Count: 12" in r.stdout and b"Tracks Listed: 8" in r.stdout,
          f"info reports true and held track counts ({r.stdout!r})")


def run_all(t, d):
    test_gapless_boxes(t, d)
    wav = os.path.join(d, "s.wav")
    make_wav(wav)
    # tiny GIF/PNG/JPEG stand-ins; only the magic bytes and size matter to the CLI
    open(os.path.join(d, "c.gif"), "wb").write(b"GIF89a" + b"\x01\x00\x01\x00\x00\x00\x00;" + b"\x00" * 8)
    open(os.path.join(d, "c.png"), "wb").write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 24)
    open(os.path.join(d, "c.jpg"), "wb").write(b"\xff\xd8\xff\xe0" + b"\x00" * 24)
    open(os.path.join(d, "c.bmp"), "wb").write(b"BM" + b"\x00" * 30)

    # ADTS and faac's own .m4a for the same audio: LC and HE-AAC v1
    for name, extra in (("lc", ["-q", "100", "--object-type", "lc"]), ("he", ["-b", "48", "--object-type", "he-aac-v1"])):
        for ext, flag in (("aac", ["-a"]), ("m4a", [])):
            r = t.run(t.faac, ["-v0"] + extra + flag + ["-o", f"{name}.{ext}", "s.wav"], d)
            check(r.returncode == 0, f"faac {name}.{ext}")

    # 1. mux correctness: gapless values from faac's own file reproduce it, and a full decode matches
    for name, sbr in (("lc", []), ("he", ["--sbr-signaling", "compatible"])):
        ref = read(os.path.join(d, f"{name}.m4a"))
        sm = smpb(ref)
        check(sm is not None, f"{name}: faac iTunSMPB present")
        delay, padding, total = sm
        out = f"{name}_faam.m4a"
        r = t.faam_run(["-i", f"{name}.aac", "-o", out, "--encoder-delay", str(delay), "--padding-delay", str(padding)] + sbr, d)
        check(r.returncode == 0 and b"Successfully muxed" in r.stdout, f"{name}: mux ok")
        mine = read(os.path.join(d, out))
        check(smpb(mine) == sm, f"{name}: iTunSMPB matches faac ({smpb(mine)} vs {sm})")
        check(elst(mine) == elst(ref), f"{name}: elst matches faac ({elst(mine)} vs {elst(ref)})")
        for tag, f in (("faac", f"{name}.m4a"), ("faam", out)):
            r = t.run(t.faad, ["-q", "--overwrite", "-o", f"{name}_{tag}.wav", f], d)
            check(r.returncode == 0, f"{name}: faad decodes the {tag} file")
        check(wav_data_len(os.path.join(d, f"{name}_faac.wav")) == wav_data_len(os.path.join(d, f"{name}_faam.wav")) > 0,
              f"{name}: faad decoded length of faam file equals faac file")
        if subprocess.run(["which", "ffmpeg"], capture_output=True).returncode == 0:
            pcm_ref = subprocess.run(["ffmpeg", "-v", "error", "-i", os.path.join(d, f"{name}.m4a"), "-f", "s16le", "-"], capture_output=True)
            pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", os.path.join(d, out), "-f", "s16le", "-"], capture_output=True)
            check(pcm_ref.returncode == 0 and pcm.returncode == 0 and len(pcm.stdout) == len(pcm_ref.stdout),
                  f"{name}: ffmpeg decoded length of faam file equals faac file")

    # without the gapless options nothing is derived or invented
    r = t.faam_run(["-i", "lc.aac", "-o", "plain.m4a"], d)
    check(r.returncode == 0 and smpb(read(os.path.join(d, "plain.m4a"))) is None, "no delay/padding: no iTunSMPB")

    test_abi_track_count(t, d)

    # 2. strict values: exit 1, a message on stderr, nothing left behind
    open(os.path.join(d, "garbage.aac"), "wb").write(bytes(range(256)) * 12)
    open(os.path.join(d, "empty.aac"), "wb").close()
    open(os.path.join(d, "text.aac"), "wb").write(b"not audio at all " * 50)
    # a valid first header whose frame never completes: zero frames
    open(os.path.join(d, "nofull.aac"), "wb").write(bytes((0xFF, 0xF1, 0x4C, 0x80, 0x0C, 0x00, 0xFC)) + b"\x00" * 10)
    # reserved sample-rate index 13
    open(os.path.join(d, "badsr.aac"), "wb").write(bytes((0xFF, 0xF1, 0x74, 0x80, 0x0C, 0x1F, 0xFC)) + b"\x00" * 90)
    bad_runs = {
        "garbage input": ["-i", "garbage.aac"],
        "empty input": ["-i", "empty.aac"],
        "text input": ["-i", "text.aac"],
        "no complete frame": ["-i", "nofull.aac"],
        "reserved sample rate": ["-i", "badsr.aac"],
        "missing input": ["-i", "nope.aac"],
        "unknown brand": ["-i", "lc.aac", "--brand", "mp3"],
        "bad sbr-signaling": ["-i", "lc.aac", "--sbr-signaling", "bogus"],
        "bad language": ["-i", "lc.aac", "--language", "english"],
        "short language": ["-i", "lc.aac", "--language", "en"],
        "numeric language": ["-i", "lc.aac", "--language", "e1g"],
        "negative delay": ["-i", "lc.aac", "--encoder-delay", "-1"],
        "text padding": ["-i", "lc.aac", "--padding-delay", "x"],
        "unknown codec": ["-i", "lc.aac", "--codec:0", "mp3"],
        "codec without input": ["-i", "lc.aac", "--codec:2", "aac"],
        "video without size": ["-i", "v.264"],
        "video without height": ["-i", "v.264", "--width", "320"],
        "zero width": ["-i", "v.264", "--width", "0", "--height", "240"],
        "bad fps": ["-i", "lc.aac", "--fps", "x"],
        "bad tag": ["-i", "lc.aac", "--tag", "novalue"],
        "bad track": ["-i", "lc.aac", "--track", "x"],
        "bad disc": ["-i", "lc.aac", "--disc", "x"],
        "bad genre": ["-i", "lc.aac", "--genre", "999"],
        "bad cover file": ["-i", "lc.aac", "--cover-art", "nope.png"],
        "unsupported cover": ["-i", "lc.aac", "--cover-art", "c.bmp"],
        "unknown option": ["-i", "lc.aac", "--bogus"],
    }
    sc = b"\x00\x00\x00\x01"
    open(os.path.join(d, "v.264"), "wb").write(
        sc + b"\x67\x42\x00\x1e\xda\x05\x07\xe8\x40" + sc + b"\x68\xce\x3c\x80" + sc + b"\x65\x88\x84\x00\x10" +
        b"".join(sc + b"\x41\x9a\x24\x6c\x42" for _ in range(4)))
    for what, args in bad_runs.items():
        out = "bad.m4a"
        r = t.faam_run(args + ["-o", out], d)
        check(r.returncode == 1 and r.stderr and no_output(d, out), f"{what}: exit 1, stderr, no output (rc={r.returncode})")

    # no mux success text on stdout when it failed
    r = t.faam_run(["-i", "garbage.aac", "-o", "bad.m4a"], d)
    check(b"Successfully" not in r.stdout, "failure does not print success")

    # accepted spellings
    for what, args in {"brand m4a": ["--brand", "m4a"], "brand M4B": ["--brand", "M4B"],
                       "language ENG": ["--language", "ENG"], "lang alias": ["--lang", "deu"],
                       "sbr explicit": ["--sbr-signaling", "explicit"]}.items():
        r = t.faam_run(["-i", "lc.aac", "-o", "ok.m4a", "--overwrite"] + args, d)
        check(r.returncode == 0, f"{what} accepted")
    r = t.faam_run(["-i", "lc.aac", "-o", "m4b.m4b", "--brand", "m4b"], d)
    check(r.returncode == 0 and b"M4B " in read(os.path.join(d, "m4b.m4b"))[:40], "brand m4b writes an M4B ftyp")

    # ID3v2-prefixed ADTS is accepted
    adts = read(os.path.join(d, "lc.aac"))
    open(os.path.join(d, "id3.aac"), "wb").write(b"ID3\x04\x00\x00\x00\x00\x00\x10" + b"\x00" * 16 + adts)
    r = t.faam_run(["-i", "id3.aac", "-o", "id3.m4a"], d)
    check(r.returncode == 0, "ID3v2 prefix skipped")
    check(t.faam_run(["info", "id3.m4a"], d).stdout.count(b"Total Frames: 236") == 1, "ID3v2 input muxes every frame")

    # language and creation time land in the file
    t.faam_run(["-i", "lc.aac", "-o", "lang.m4a", "--language", "DEU", "--creation-time", "1700000000"], d)
    lang = read(os.path.join(d, "lang.m4a"))
    t.run(t.faac, ["-v0", "-q", "100", "--lang", "deu", "--creation-time", "1700000000", "-o", "lang_faac.m4a", "s.wav"], d)
    check(mdhd_lang(lang) == mdhd_lang(read(os.path.join(d, "lang_faac.m4a"))), "language packs like faac")
    check(mvhd_ctime(lang) == 1700000000 + 2082844800, "creation time stored")
    r = t.faam_run(["-i", "lc.aac", "-o", "sde.m4a"], d, {"SOURCE_DATE_EPOCH": "1600000000"})
    check(mvhd_ctime(read(os.path.join(d, "sde.m4a"))) == 1600000000 + 2082844800, "SOURCE_DATE_EPOCH used when no option")
    r = t.faam_run(["-i", "lc.aac", "-o", "auto.m4a", "--creation-time", "auto"], d)
    os.utime(os.path.join(d, "lc.aac"), (1500000000, 1500000000))
    r = t.faam_run(["-i", "lc.aac", "-o", "auto.m4a", "--creation-time", "auto", "--overwrite"], d)
    check(mvhd_ctime(read(os.path.join(d, "auto.m4a"))) == 1500000000 + 2082844800, "creation-time auto is the input mtime")
    r = t.faam_run(["-i", "lc.aac", "-o", "badct.m4a", "--creation-time", "soon"], d)
    check(r.returncode == 0 and b"invalid creation time" in r.stderr and mvhd_ctime(read(os.path.join(d, "badct.m4a"))) == 0,
          "invalid creation time warns and stores 0 like faac")

    # Unix times past 2040 fit as version-1 boxes up to 2^32 - 1; anything else is refused, never truncated
    r = t.faam_run(["-i", "lc.aac", "-o", "y2100.m4a", "--creation-time", "4102444800"], d)
    check(r.returncode == 0 and mvhd_ctime_any(read(os.path.join(d, "y2100.m4a"))) == 4102444800 + 2082844800,
          "creation time in 2100 stored in full")
    for what, spec in {"year 2200": "7258118400", "2^32": "4294967296", "negative": "-1"}.items():
        for exe, args, out in ((t.faam, ["-i", "lc.aac", "-o", "oor.m4a"], "oor.m4a"),
                               (t.faac, ["-v0", "-q", "100", "-o", "oor_faac.m4a", "s.wav"], "oor_faac.m4a")):
            name = os.path.basename(exe)
            r = t.run(exe, args + ["--creation-time", spec], d)
            check(r.returncode != 0 and b"creation time" in r.stderr and b"out of range" in r.stderr and
                  not os.path.exists(os.path.join(d, out)), f"{name} refuses --creation-time {what}")
            r = t.run(exe, args, d, {"SOURCE_DATE_EPOCH": spec})
            check(r.returncode != 0 and b"SOURCE_DATE_EPOCH" in r.stderr and
                  not os.path.exists(os.path.join(d, out)), f"{name} refuses SOURCE_DATE_EPOCH {what}")
    r = t.faam_run(["-i", "lc.aac", "-o", "max32.m4a", "--creation-time", "4294967295"], d)
    check(r.returncode == 0 and mvhd_ctime_any(read(os.path.join(d, "max32.m4a"))) == 4294967295 + 2082844800,
          "largest 32-bit Unix time stored in full")

    # 3. overwrite protection
    r1 = t.faam_run(["-i", "lc.aac", "-o", "ow.m4a"], d)
    before = read(os.path.join(d, "ow.m4a"))
    r2 = t.faam_run(["-i", "he.aac", "-o", "ow.m4a"], d)
    check(r1.returncode == 0 and r2.returncode == 1 and b"already exists (use --overwrite)" in r2.stderr and
          read(os.path.join(d, "ow.m4a")) == before, "mux -o refuses to overwrite and leaves the file alone")
    r3 = t.faam_run(["-i", "he.aac", "-o", "ow.m4a", "--overwrite"], d)
    check(r3.returncode == 0 and read(os.path.join(d, "ow.m4a")) != before, "mux --overwrite replaces")

    work = os.path.join(d, "dm")
    os.makedirs(work)
    r = t.faam_run(["demux", "../lc.m4a", "-o", "out.aac"], work)
    check(r.returncode == 0, "demux -o")
    r = t.faam_run(["demux", "../lc.m4a", "-o", "out.aac"], work)
    check(r.returncode == 1 and b"already exists (use --overwrite)" in r.stderr, "demux -o refuses to overwrite")
    r = t.faam_run(["demux", "../lc.m4a", "-o", "out.aac", "--overwrite"], work)
    check(r.returncode == 0, "demux --overwrite")
    r = t.faam_run(["demux", "../lc.m4a"], work)
    check(r.returncode == 0 and os.path.exists(os.path.join(work, "output.raw")), "demux default output.raw")
    r = t.faam_run(["demux", "../lc.m4a"], work)
    check(r.returncode == 1 and b"output.raw already exists" in r.stderr, "demux default output.raw is protected")
    r = t.faam_run(["demux", "../lc.m4a", "--export-asc", "asc.bin", "-o", "o2.aac"], work)
    check(r.returncode == 0 and read(os.path.join(work, "asc.bin")) == bytes.fromhex("1190"), "demux --export-asc (48 kHz LC stereo ASC)")
    r = t.faam_run(["demux", "../lc.m4a", "--export-asc", "asc.bin", "-o", "o3.aac"], work)
    check(r.returncode == 1 and b"asc.bin already exists" in r.stderr and no_output(work, "o3.aac"),
          "--export-asc is protected and nothing else is written")
    r = t.faam_run(["demux", "../lc.m4a", "--export-asc", "asc.bin", "-o", "o4.aac", "--overwrite"], work)
    check(r.returncode == 0, "demux --export-asc --overwrite")
    for args in (["-t", "99"], ["-t", "abc"], ["-t", "0"]):
        r = t.faam_run(["demux", "../lc.m4a", "-o", "o5.aac"] + args, work)
        check(r.returncode == 1 and no_output(work, "o5.aac"), f"demux {args} rejected")
    # the demuxed stream decodes to the same audio as the original ADTS
    a = t.run(t.faad, ["-q", "--overwrite", "-o", "a.wav", "../lc.aac"], work)
    b = t.run(t.faad, ["-q", "--overwrite", "-o", "b.wav", "out.aac"], work)
    check(a.returncode == 0 and b.returncode == 0 and read(os.path.join(work, "a.wav")) == read(os.path.join(work, "b.wav")),
          "demux round trip decodes identically")

    # 4. metadata at mux time equals the same options through tag
    opts = ["--title", " 'The Title' ", "--titlesort", "Title, The", "--artist", "Art", "--album", "Alb",
            "--albumartist", "AA", "--composer", "Comp", "--artistsort", "As", "--albumsort", "Als",
            "--albumartistsort", "AAs", "--composersort", "Cs", "--year", "2026", "--comment", "a comment",
            "--genre", "Rock", "--compilation", "--track", "3/12", "--disc", "1/2", "--cover-art", "c.gif",
            "--tag", "my key=my value", "--tag", "other,second"]
    r = t.faam_run(["-i", "lc.aac", "-o", "mt.m4a"] + opts, d)
    check(r.returncode == 0, "mux with every tag option")
    r = t.faam_run(["-i", "lc.aac", "-o", "bare.m4a"], d)
    r = t.faam_run(["tag", "bare.m4a"] + opts, d)
    check(r.returncode == 0, "tag with every tag option")
    mux_tags = info_tags(t.faam_run(["info", "mt.m4a"], d).stdout)
    tag_tags = info_tags(t.faam_run(["info", "bare.m4a"], d).stdout)
    check(mux_tags == tag_tags and "Title: The Title" in mux_tags and "Title Sort: Title, The" in mux_tags and
          "Genre: Rock" in mux_tags and "Track: 3/12" in mux_tags and "Disc: 1/2" in mux_tags and
          "Cover Art: present" in mux_tags and "my key = my value" in mux_tags and "other = second" in mux_tags and
          "Compilation: Yes" in mux_tags, "mux-time tags equal tag-command tags and show in info:\n" + mux_tags)
    check(read(os.path.join(d, "mt.m4a")).count(b"GIF89a") == 1, "GIF cover stored as given")
    for cover in ("c.png", "c.jpg"):
        r = t.faam_run(["-i", "lc.aac", "-o", "cv.m4a", "--cover-art", cover, "--overwrite"], d)
        check(r.returncode == 0, f"cover {cover}")
    # same cover atom bytes as faac
    t.run(t.faac, ["-v0", "-q", "100", "--cover-art", "c.gif", "--creation-time", "0", "-o", "cv_faac.m4a", "s.wav"], d)
    cov = lambda b: b[b.index(b"covr") - 4:][: struct.unpack_from(">I", b, b.index(b"covr") - 4)[0]]
    check(cov(read(os.path.join(d, "cv_faac.m4a"))) == cov(read(os.path.join(d, "mt.m4a"))), "GIF covr atom equals faac's")
    # separators and trimming like faac
    r = t.faam_run(["-i", "lc.aac", "-o", "sep.m4a", "--tag", " 'k1' = ' v1 ' ", "--tag", "k2,v=2", "--tag", "k3=v,3"], d)
    st = t.faam_run(["info", "sep.m4a"], d).stdout.decode()
    check("k1 = v1" in st and "k2 = v=2" in st and "k3 = v,3" in st, "--tag splits on the first of = or , and trims quotes:\n" + st)
    for args in (["--tag", "=x"], ["--tag", "k="]):
        r = t.faam_run(["-i", "lc.aac", "-o", "badtag.m4a"] + args, d)
        check(r.returncode == 1 and no_output(d, "badtag.m4a"), f"{args} rejected")
    # no cap on custom tags: 20 survive a mux and a later tag edit, from faam or from faac
    many = sum((["--tag", f"k{i}=v{i}"] for i in range(20)), [])
    r = t.faam_run(["-i", "lc.aac", "-o", "many.m4a"] + many, d)
    check(r.returncode == 0 and t.faam_run(["info", "many.m4a"], d).stdout.count(b" = ") == 20, "20 custom tags at mux time")
    t.faam_run(["tag", "many.m4a", "--title", "x"], d)
    check(t.faam_run(["info", "many.m4a"], d).stdout.count(b" = ") == 20, "tag keeps all 20 custom tags")
    t.run(t.faac, ["-v0", "-q", "100", "-o", "many_faac.m4a"] + many + ["s.wav"], d)
    t.faam_run(["tag", "many_faac.m4a", "--title", "x"], d)
    check(t.faam_run(["info", "many_faac.m4a"], d).stdout.count(b" = ") == 20, "tag keeps the 20 custom tags of a faac file")
    # genre as number, free text
    for g, want in (("17", "Genre: Rock"), ("Jazz", "Genre: Jazz"), ("NotAGenre", "Genre: NotAGenre")):
        t.faam_run(["-i", "lc.aac", "-o", "g.m4a", "--genre", g, "--overwrite"], d)
        gi = t.faam_run(["info", "g.m4a"], d).stdout.decode()
        check(want in gi, f"genre {g} -> {want}")
    r = t.faam_run(["tag", "g.m4a", "--genre", "256"], d)
    check(r.returncode == 1, "tag --genre out of range")
    # shared option table: faac and faam agree on the validation they share
    for args in (["--track", "x"], ["--disc", "z"], ["--cover-art", "nope.png"], ["--cover-art", "c.bmp"], ["--tag", "novalue"]):
        rf = t.run(t.faac, ["-v0", "-o", "e.m4a", "--overwrite"] + args + ["s.wav"], d)
        rm = t.faam_run(["-i", "lc.aac", "-o", "e2.m4a", "--overwrite"] + args, d)
        check(rf.returncode == 1 and rm.returncode == 1 and rf.stderr.strip() == rm.stderr.strip(),
              f"{args}: faac and faam give the same error ({rf.stderr!r} vs {rm.stderr!r})")

    # 5/6. tag edits in place, then info; --lang is gone from tag; --remove is strict
    shutil_copy(os.path.join(d, "lc.m4a"), os.path.join(d, "t.m4a"))
    r = t.faam_run(["tag", "t.m4a", "--lang", "eng"], d)
    check(r.returncode == 1, "tag --lang rejected")
    r = t.faam_run(["tag", "t.m4a", "--remove", "bogus"], d)
    check(r.returncode == 1 and b"bogus" in r.stderr, "tag --remove unknown field is an error")
    before = read(os.path.join(d, "t.m4a"))
    r = t.faam_run(["tag", "t.m4a", "--title", "One", "--tag", "a=b"], d)
    r = t.faam_run(["tag", "t.m4a", "--album", "Two", "--titlesort", "TS"], d)
    ti = info_tags(t.faam_run(["info", "t.m4a"], d).stdout)
    check("Title: One" in ti and "Album: Two" in ti and "Title Sort: TS" in ti and "a = b" in ti, "tag updates accumulate:\n" + ti)
    r = t.faam_run(["tag", "t.m4a", "--remove", "title", "--remove", "titlesort", "--remove", "custom"], d)
    ti = info_tags(t.faam_run(["info", "t.m4a"], d).stdout)
    check("Title:" not in ti and "Title Sort" not in ti and "a = b" not in ti and "Album: Two" in ti, "tag --remove:\n" + ti)
    r = t.faam_run(["tag", "t.m4a", "--clear", "--artist", "Only"], d)
    ti = info_tags(t.faam_run(["info", "t.m4a"], d).stdout)
    check("Artist: Only" in ti and "Album" not in ti, "tag --clear:\n" + ti)
    # tag -h lists tag options only
    h = t.faam_run(["tag", "-h"], d).stdout.decode()
    check("--titlesort" in h and "--width" not in h and "--fps" not in h, "faam tag -h shows tag options only")
    mh = t.faam_run(["--help-mux"], d).stdout.decode()
    check("--overwrite" in mh and "--fps" in mh and "--language" in mh and "--creation-time" in mh, "mux help lists the new options")

    # udta-less file: patch the udta atom's type to free
    m = bytearray(read(os.path.join(d, "plain.m4a")))
    udta = find(m, ("moov", "udta"))
    check(udta is not None, "plain.m4a has a udta to patch")
    pos = udta[0] - 4
    m[pos:pos + 4] = b"free"
    open(os.path.join(d, "noudta.m4a"), "wb").write(m)
    check(find(m, ("moov", "udta")) is None, "patched file has no udta")
    r = t.faam_run(["tag", "noudta.m4a", "--title", "Created", "--artist", "A"], d)
    ti = info_tags(t.faam_run(["info", "noudta.m4a"], d).stdout)
    check(r.returncode == 0 and "Title: Created" in ti, "tag creates a missing udta:\n" + ti)
    r = t.run(t.faad, ["-q", "--overwrite", "-o", "noudta.wav", "noudta.m4a"], d)
    check(r.returncode == 0, "udta-less file decodes after tagging")
    open(os.path.join(d, "noudta2.m4a"), "wb").write(m)
    open(os.path.join(d, "c1.txt"), "w").write("00:00:00.000\tIntro\n00:00:02.500\tNext\n")
    r = t.faam_run(["chapter", "import", "noudta2.m4a", "--chapters", "c1.txt"], d)
    check(r.returncode == 0 and b"Chapter #2: start=2500 ms, title=\"Next\"" in t.faam_run(["info", "noudta2.m4a"], d).stdout,
          "chapter import creates a missing udta")

    # chapters: up to 255, an error above, info lists them all, export round trips
    for n in (64, 65, 100, 255):
        with open(os.path.join(d, f"ch{n}.txt"), "w") as f:
            for i in range(n):
                f.write("%02d:%02d:%02d.%03d\tChapter %d\n" % (i // 3600, (i // 60) % 60, i % 60, (i * 7) % 1000, i))
        shutil_copy(os.path.join(d, "lc_faam.m4a"), os.path.join(d, f"cc{n}.m4a"))
        r = t.faam_run(["chapter", "import", f"cc{n}.m4a", "--chapters", f"ch{n}.txt"], d)
        check(r.returncode == 0, f"import {n} chapters")
        inf = t.faam_run(["info", f"cc{n}.m4a"], d).stdout.decode()
        check(inf.count("Chapter #") == n and f"({n} entries)" in inf, f"info lists all {n} chapters")
        r = t.faam_run(["chapter", "export", f"cc{n}.m4a", "-o", f"exp{n}.txt"], d)
        check(r.returncode == 0 and read(os.path.join(d, f"exp{n}.txt")) == read(os.path.join(d, f"ch{n}.txt")), f"export {n} chapters round trips")
        check(t.run(t.faad, ["-q", "--overwrite", "-o", f"cc{n}.wav", f"cc{n}.m4a"], d).returncode == 0, f"{n}-chapter file decodes")
    with open(os.path.join(d, "ch256.txt"), "w") as f:
        for i in range(256):
            f.write("00:%02d:%02d.000\tChapter %d\n" % ((i // 60) % 60, i % 60, i))
    shutil_copy(os.path.join(d, "lc_faam.m4a"), os.path.join(d, "cc256.m4a"))
    r = t.faam_run(["chapter", "import", "cc256.m4a", "--chapters", "ch256.txt"], d)
    check(r.returncode == 1 and b"max 255" in r.stderr, "256 chapters rejected")
    # chapter export: overwrite protection and no chapters
    r = t.faam_run(["chapter", "export", "cc64.m4a", "-o", "exp64.txt"], d)
    check(r.returncode == 1 and b"already exists (use --overwrite)" in r.stderr, "chapter export refuses to overwrite")
    r = t.faam_run(["chapter", "export", "cc64.m4a", "-o", "exp64.txt", "--overwrite"], d)
    check(r.returncode == 0, "chapter export --overwrite")
    r = t.faam_run(["chapter", "export", "lc_faam.m4a", "-o", "nochap.txt"], d)
    check(r.returncode == 1 and b"No chapters" in r.stderr and no_output(d, "nochap.txt"), "chapter export of a file without chapters is an error")
    # tag and chapter import edit in place without --overwrite
    r = t.faam_run(["chapter", "import", "cc64.m4a", "--chapters", "c1.txt"], d)
    check(r.returncode == 0, "chapter import in place needs no --overwrite")

    # failed mux leaves no partial output when the sink fails (Linux only: /dev/full)
    if os.path.exists("/dev/full"):
        r = t.faam_run(["-i", "lc.aac", "-o", "/dev/full", "--overwrite"], d)
        check(r.returncode == 1 and b"Successfully" not in r.stdout and os.path.exists("/dev/full"),
              "write failure: exit 1, no success text, device kept")

    # video: needs a -Dmuxer-video build
    r = t.faam_run(["-i", "v.264", "-o", "vid.mp4", "--width", "320", "--height", "240", "--fps", "25"], d)
    have_video = r.returncode == 0
    if have_video:
        r = t.faam_run(["-i", "v.264", "-o", "rotate.mp4", "--width", "320", "--height", "240", "--rotation", "90"], d)
        check(r.returncode == 0 and b"Rotation: 90 degrees" in t.faam_run(["info", "rotate.mp4"], d).stdout,
              "rotation 90 round-trips through info")
        r = t.faam_run(["-i", "v.264", "-o", "rotate_bad.mp4", "--width", "320", "--height", "240", "--rotation", "45"], d)
        check(r.returncode == 1 and r.stderr, "rotation 45 rejected")
        check({x[1] for x in stts(read(os.path.join(d, "vid.mp4")))} == {3600}, "video --fps 25 gives 3600 ticks per frame")
        r = t.faam_run(["-i", "v.264", "-o", "vid2.mp4", "--width", "320", "--height", "240"], d)
        check(r.returncode == 0 and {x[1] for x in stts(read(os.path.join(d, "vid2.mp4")))} == {3000}, "video default 30 fps gives 3000 ticks")
        r = t.faam_run(["-i", "v.264", "-o", "vid3.mp4", "--width", "320", "--height", "240", "--fps", "30000/1001"], d)
        check(r.returncode == 0 and {x[1] for x in stts(read(os.path.join(d, "vid3.mp4")))} <= {3003}, "video 30000/1001 gives 3003 ticks")
        # an access unit far above 64 KB must demux and re-mux unchanged
        big = (sc + b"\x67\x42\x00\x1e\xda\x05\x07\xe8\x40" + sc + b"\x68\xce\x3c\x80" + sc + b"\x65\x88" + b"\x55" * 300000 +
               sc + b"\x41\x9a" + b"\x55" * 5000)
        open(os.path.join(d, "big.264"), "wb").write(big)
        o = ["--width", "1920", "--height", "1080", "--fps", "25"]
        r1 = t.faam_run(["-i", "big.264", "-o", "big1.mp4"] + o, d)
        r2 = t.faam_run(["demux", "big1.mp4", "-o", "bigd.264"], d)
        r3 = t.faam_run(["-i", "bigd.264", "-o", "big2.mp4"] + o, d)
        check(r1.returncode == r2.returncode == r3.returncode == 0 and read(os.path.join(d, "big1.mp4")) == read(os.path.join(d, "big2.mp4")),
              "video access unit over 64 KB demuxes and re-muxes identically")
        check(b"\x55" * 300000 in read(os.path.join(d, "bigd.264")), "demuxed video carries the whole large frame")
        open(os.path.join(d, "empty.264"), "wb").close()
        r = t.faam_run(["-i", "empty.264", "-o", "vid4.mp4", "--width", "320", "--height", "240"], d)
        check(r.returncode == 1 and no_output(d, "vid4.mp4"), "empty video input is an error")
    else:
        print("note: video checks skipped (faam built without muxer-video)")

    run_demux_header(t, d)
    run_adts_validation(t, d)
    run_info_and_chapters(t, d, have_video)
    run_tag_safety(t, d)
    run_same_file(t, d)
    run_unmodeled_preservation(t, d)
    if have_video:
        run_video_packetizer(t, d)
    run_large_files(t, d)


def run_unmodeled_preservation(t, d):
    if subprocess.run(["which", "ffmpeg"], capture_output=True).returncode == 0:
        wav = os.path.join(d, "s.wav")
        ff_out = os.path.join(d, "ff_meta.m4a")
        r = subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", wav, "-c:a", "aac",
                            "-metadata", "grouping=GGroup", "-metadata", "description=DDesc",
                            "-metadata", "composer=CComp", ff_out], capture_output=True)
        if r.returncode == 0:
            # 1. Edit title on ffmpeg file with unmodeled tags (grouping, description)
            r = t.faam_run(["tag", "ff_meta.m4a", "--title", "New FF Title"], d)
            check(r.returncode == 0, "faam tag on ffmpeg file with grouping/description succeeds")
            m_bytes = read(ff_out)
            check(b"GGroup" in m_bytes, "ffmpeg grouping atom (©grp) preserved after faam tag")
            check(b"DDesc" in m_bytes, "ffmpeg description atom (desc) preserved after faam tag")
            check(b"New FF Title" in m_bytes, "title updated on ffmpeg file")

        # 2. ffmpeg file with QuickTime keys (-movflags use_metadata_tags) -> mdta handler
        ff_mdta = os.path.join(d, "ff_mdta.m4a")
        r = subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", wav, "-c:a", "aac",
                            "-metadata", "grouping=GGroup", "-movflags", "use_metadata_tags", ff_mdta], capture_output=True)
        if r.returncode == 0:
            before = read(ff_mdta)
            r = t.faam_run(["tag", "ff_mdta.m4a", "--title", "Should Fail"], d)
            check(r.returncode != 0, "faam tag on mdta QuickTime keys returns error")
            check(read(ff_mdta) == before, "mdta QuickTime keys file untouched on error")


def slurp(path):
    """read() for a file a failing run may have deleted."""
    try:
        return read(path)
    except FileNotFoundError:
        return None


def sha(path):
    return hashlib.sha256(read(path)).hexdigest()


def adts_frames(path):
    """(profile, sample-rate index, channel configuration, payload) of every ADTS frame."""
    d = read(path)
    pos, frames = 0, []
    while pos + 7 <= len(d):
        if d[pos] == 0xFF and (d[pos + 1] & 0xF6) == 0xF0:
            hlen = 7 if d[pos + 1] & 1 else 9
            n = ((d[pos + 3] & 3) << 11) | (d[pos + 4] << 3) | (d[pos + 5] >> 5)
            frames.append(((d[pos + 2] >> 6) & 3, (d[pos + 2] >> 2) & 15, ((d[pos + 2] & 1) << 2) | (d[pos + 3] >> 6),
                           d[pos + hlen:pos + n]))
            pos += n
        else:
            pos += 1
    return frames


def adts_chunks(path):
    d = read(path)
    pos, out = 0, []
    while pos + 7 <= len(d):
        n = ((d[pos + 3] & 3) << 11) | (d[pos + 4] << 3) | (d[pos + 5] >> 5)
        out.append(bytearray(d[pos:pos + n]))
        pos += n
    return out


def decode_pcm(t, d, src, name):
    r = t.run(t.faad, ["-q", "--overwrite", "-o", name, src], d)
    return read(os.path.join(d, name)) if r.returncode == 0 else None


def ffmpeg_pcm_len(path):
    if subprocess.run(["which", "ffmpeg"], capture_output=True).returncode != 0:
        return None
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "s16le", "-"], capture_output=True)
    return len(r.stdout) if r.returncode == 0 else None


def run_demux_header(t, d):
    """The ADTS header demux writes comes from the track's AudioSpecificConfig: core object type, core
    rate and the real channel configuration, whatever way SBR/PS was signalled."""
    make_wav(os.path.join(d, "mono.wav"), channels=1)
    r = t.run(t.faac, ["-v0", "-q", "100", "--object-type", "lc", "-a", "-o", "mono.aac", "mono.wav"], d)
    check(r.returncode == 0, "faac mono.aac")
    rnd = random.Random(7)
    with open(os.path.join(d, "c8.raw"), "wb") as f:
        f.write(b"".join(struct.pack("<h", int(6000 * math.sin(i * (0.02 + 0.013 * (i % 8))) + rnd.randint(-900, 900)))
                         for i in range(48000 * 8)))
    r = t.run(t.faac, ["-v0", "-P", "-R", "48000", "-B", "16", "-C", "8", "--object-type", "lc", "-q", "100", "-a",
                       "-o", "c8.aac", "c8.raw"], d)
    check(r.returncode == 0 and adts_frames(os.path.join(d, "c8.aac"))[0][2] == 7, "faac 7.1 ADTS has channel configuration 7")

    cases = [("lc.aac", m) for m in ("none", "compatible", "explicit")] + \
            [("he.aac", m) for m in ("none", "compatible", "explicit")] + \
            [("mono.aac", m) for m in ("none", "ps", "ps-explicit")] + \
            [("c8.aac", m) for m in ("none", "compatible", "explicit")]
    for src, mode in cases:
        tag = f"{src}/{mode}"
        base = f"rt_{src[:-4]}_{mode}"
        r = t.faam_run(["-i", src, "-o", base + ".m4a", "--sbr-signaling", mode], d)
        check(r.returncode == 0, f"{tag}: mux")
        r = t.faam_run(["demux", base + ".m4a", "-o", base + ".aac"], d)
        check(r.returncode == 0, f"{tag}: demux to ADTS")
        if r.returncode != 0:
            continue
        a, b = adts_frames(os.path.join(d, src)), adts_frames(os.path.join(d, base + ".aac"))
        check(a == b and len(a) > 0, f"{tag}: demuxed ADTS frames match the source in profile, rate, channels and payload")
        if a != b and a and b:
            check(False, f"{tag}: first header {a[0][:3]} vs {b[0][:3]}")
        if mode in ("ps", "ps-explicit"):
            continue
        src_pcm = decode_pcm(t, d, src, base + "_src.wav")
        out_pcm = decode_pcm(t, d, base + ".aac", base + "_out.wav")
        check(src_pcm is not None and src_pcm == out_pcm, f"{tag}: faad decodes the demuxed ADTS like the source")
        if mode == "none" and not src.startswith("he"):
            # (an SBR stream differs by the decoder delay faad removes from an MP4 only)
            m4a_pcm = decode_pcm(t, d, base + ".m4a", base + "_m4a.wav")
            check(m4a_pcm == out_pcm, f"{tag}: faad decodes the .m4a and the demuxed .aac identically")
            n1, n2 = ffmpeg_pcm_len(os.path.join(d, base + ".m4a")), ffmpeg_pcm_len(os.path.join(d, base + ".aac"))
            if n1 is not None:
                check(n1 == n2, f"{tag}: ffmpeg decodes {n1} and {n2} bytes from the .m4a and the demuxed .aac")

    # a configuration an ADTS header cannot carry is an error, not a corrupted header
    lc = bytearray(read(os.path.join(d, "lc.m4a")))
    esds = b"\x02\x11\x90\x06"  # end of the DecoderSpecificInfo (AAC-LC, 48 kHz, stereo) and the SLConfig tag
    check(lc.count(esds) == 1, "lc.m4a carries its ASC once")
    for what, asc in (("object type 17", b"\x02\x89\x90\x06"), ("reserved rate index 13", b"\x02\x16\x90\x06")):
        bad = bytes(lc).replace(esds, asc)
        open(os.path.join(d, "badasc.m4a"), "wb").write(bad)
        r = t.faam_run(["demux", "badasc.m4a", "-o", "badasc.aac", "--overwrite"], d)
        check(r.returncode == 1 and r.stderr and no_output(d, "badasc.aac"), f"demux to ADTS refuses {what} (rc={r.returncode})")
        r = t.faam_run(["demux", "badasc.m4a", "-o", "badasc.raw", "--overwrite"], d)
        check(r.returncode == 0, f"raw demux of {what} still works")


def run_adts_validation(t, d):
    frames = adts_chunks(os.path.join(d, "lc.aac"))
    he_frames = adts_chunks(os.path.join(d, "he.aac"))
    mono_frames = adts_chunks(os.path.join(d, "mono.aac"))

    def stream(name, chunks):
        open(os.path.join(d, name), "wb").write(b"".join(bytes(c) for c in chunks))

    # more than one raw data block per ADTS frame cannot be one 1024-tick sample
    multi = [bytearray(c) for c in frames[:20]]
    for c in multi:
        c[6] |= 1
    stream("multi.aac", multi)
    r = t.faam_run(["-i", "multi.aac", "-o", "multi.m4a"], d)
    check(r.returncode == 1 and b"raw data block" in r.stderr and no_output(d, "multi.m4a"),
          f"ADTS with several raw data blocks per frame is rejected ({r.stderr!r})")

    # ...also when it only shows up after a good start
    later = [bytearray(c) for c in frames[20:30]]
    for c in later:
        c[6] |= 1
    stream("later_multi.aac", frames[:10] + later)
    r = t.faam_run(["-i", "later_multi.aac", "-o", "later_multi.m4a"], d)
    check(r.returncode == 1 and b"raw data block" in r.stderr and no_output(d, "later_multi.m4a"),
          f"a later ADTS frame with several raw data blocks fails the mux ({r.stderr!r})")

    # frames of another configuration are false syncs, not samples
    other_profile = [bytearray(c) for c in frames[10:15]]
    for c in other_profile:
        c[2] &= 0x3F  # Main profile
    stream("mixed.aac", frames[:10] + other_profile + frames[15:25] + he_frames[:10] + mono_frames[:10])
    r = t.faam_run(["-i", "mixed.aac", "-o", "mixed.m4a"], d)
    info = t.faam_run(["info", "mixed.m4a"], d).stdout
    check(r.returncode == 0 and b"Total Frames: 20\n" in info and b"Channels: 2" in info,
          f"frames whose profile, rate or channels differ from the first are skipped:\n{info.decode()}")

    # HE-AAC v2 has a mono core
    for mode in ("ps", "ps-explicit"):
        r = t.faam_run(["-i", "lc.aac", "-o", "ps_stereo.m4a", "--sbr-signaling", mode, "--overwrite"], d)
        check(r.returncode == 1 and r.stderr and b"mono" in r.stderr, f"{mode} on a stereo core is rejected ({r.stderr!r})")
        r = t.faam_run(["-i", "mono.aac", "-o", f"ps_{mode}.m4a", "--sbr-signaling", mode, "--overwrite"], d)
        check(r.returncode == 0, f"{mode} on a mono core is accepted")


def run_info_and_chapters(t, d, have_video):
    # chapter fractions are decimal fractions of a second
    open(os.path.join(d, "frac.txt"), "w").write("00:00:01.5\tHalf\n00:00:02.05\tFiftieth\n00:00:03.123\tExact\n00:00:04.50\tTwo\n")
    shutil_copy(os.path.join(d, "lc_faam.m4a"), os.path.join(d, "frac.m4a"))
    r = t.faam_run(["chapter", "import", "frac.m4a", "--chapters", "frac.txt"], d)
    inf = t.faam_run(["info", "frac.m4a"], d).stdout.decode()
    check(r.returncode == 0 and "start=1500 ms" in inf and "start=2050 ms" in inf and "start=3123 ms" in inf and "start=4500 ms" in inf,
          "chapter fractions scale by digit count (.5 = 500 ms, .05 = 50 ms):\n" + inf)
    for what, line in (("four fractional digits", "00:00:01.1234\tX\n"), ("no fraction", "00:00:01\tX\n"),
                       ("empty fraction", "00:00:01.\tX\n"), ("signed fraction", "00:00:01.-5\tX\n")):
        open(os.path.join(d, "badfrac.txt"), "w").write(line)
        before = sha(os.path.join(d, "frac.m4a"))
        r = t.faam_run(["chapter", "import", "frac.m4a", "--chapters", "badfrac.txt"], d)
        check(r.returncode == 1 and sha(os.path.join(d, "frac.m4a")) == before, f"chapter line with {what} is rejected")

    # info names the real brand and object type
    def info_line(name, prefix):
        out = t.faam_run(["info", name], d).stdout.decode()
        return next((l for l in out.splitlines() if l.strip().startswith(prefix)), "")

    check("M4A" in info_line("lc.m4a", "Container") and "MP4V" not in info_line("lc.m4a", "Container"),
          "info shows the M4A brand: " + info_line("lc.m4a", "Container"))
    check("M4B" in info_line("m4b.m4b", "Container"), "info shows the M4B brand: " + info_line("m4b.m4b", "Container"))
    other = bytearray(read(os.path.join(d, "lc_faam.m4a")))
    other[8:12] = b"mp42"
    open(os.path.join(d, "mp42.m4a"), "wb").write(other)
    check("mp42" in info_line("mp42.m4a", "Container"), "info shows an unusual brand: " + info_line("mp42.m4a", "Container"))
    check("AAC-LC" in info_line("lc.m4a", "AAC Object Type"), "LC is labelled AAC-LC")
    for mode in ("compatible", "explicit"):
        t.faam_run(["-i", "he.aac", "-o", f"he_{mode}.m4a", "--sbr-signaling", mode, "--overwrite"], d)
        line = info_line(f"he_{mode}.m4a", "AAC Object Type")
        check("HE-AAC v1" in line and "v2" not in line, f"{mode} SBR is labelled HE-AAC v1: {line}")
    t.faam_run(["-i", "mono.aac", "-o", "ps_label.m4a", "--sbr-signaling", "ps", "--overwrite"], d)
    check("HE-AAC v2" in info_line("ps_label.m4a", "AAC Object Type"), "PS is labelled HE-AAC v2")
    odd = bytes(read(os.path.join(d, "lc.m4a"))).replace(b"\x02\x11\x90\x06", b"\x02\x89\x90\x06")
    open(os.path.join(d, "odd.m4a"), "wb").write(odd)
    line = info_line("odd.m4a", "AAC Object Type")
    check("17" in line and "HE-AAC" not in line and "AAC-LC" not in line, "an unknown object type shows its number only: " + line)

    # gapless values need an audio track to land on
    if have_video:
        for opt in ("--encoder-delay", "--padding-delay"):
            r = t.faam_run(["-i", "v.264", "-o", "novideo.mp4", "--width", "320", "--height", "240", opt, "10"], d)
            check(r.returncode == 1 and r.stderr and no_output(d, "novideo.mp4"), f"{opt} without an audio track is an error")
        r = t.faam_run(["-i", "v.264", "-i", "lc.aac", "-o", "av.mp4", "--width", "320", "--height", "240",
                        "--encoder-delay", "1024", "--padding-delay", "5"], d)
        check(r.returncode == 0, "--encoder-delay with an audio track is fine")


def run_tag_safety(t, d):
    t.faam_run(["-i", "lc.aac", "-o", "keep.m4a", "--title", "Keep", "--artist", "Me", "--overwrite"], d)
    good = read(os.path.join(d, "keep.m4a"))
    # a moov the demuxer will not load (over its size cap) while the in-place rewriter still can find it
    moov_at = find(good, ("moov",))[0] - 8
    big_moov = bytearray(good)
    big_moov[moov_at:moov_at + 4] = struct.pack(">I", 0x14000000)
    bad = {
        "unloadable moov": bytes(big_moov),
        "truncated file": good[:len(good) // 2],
        "no moov": good[:moov_at],
        "no tracks": good.replace(b"trak", b"free", 1),
        "not an mp4": b"RIFF" + b"\x00" * 300,
    }
    for what, data in bad.items():
        open(os.path.join(d, "untouchable.m4a"), "wb").write(data)
        before = sha(os.path.join(d, "untouchable.m4a"))
        r = t.faam_run(["tag", "untouchable.m4a", "--album", "New"], d)
        check(r.returncode == 1 and r.stderr and sha(os.path.join(d, "untouchable.m4a")) == before,
              f"tag on {what}: exit 1, an error, file untouched (rc={r.returncode})")
        r = t.faam_run(["tag", "untouchable.m4a", "--album", "New", "--remove", "title"], d)
        check(r.returncode == 1 and sha(os.path.join(d, "untouchable.m4a")) == before,
              f"tag --remove on {what}: refused, file untouched")
    # --clear asks for an empty list, so the old tags are not needed
    r = t.faam_run(["tag", "keep.m4a", "--clear", "--artist", "Only"], d)
    check(r.returncode == 0 and "Artist: Only" in info_tags(t.faam_run(["info", "keep.m4a"], d).stdout), "tag --clear still works")
    # a moov with a 64-bit size header cannot be grown in place
    wide = bytearray(good)
    msize = struct.unpack_from(">I", good, moov_at)[0]
    wide[moov_at:moov_at + msize] = struct.pack(">I4sQ", 1, b"moov", msize + 8) + good[moov_at + 8:moov_at + msize]
    open(os.path.join(d, "wide.m4a"), "wb").write(wide)
    before = sha(os.path.join(d, "wide.m4a"))
    r = t.faam_run(["tag", "wide.m4a", "--title", "A much longer title than before"], d)
    check(r.returncode == 1 and r.stderr and sha(os.path.join(d, "wide.m4a")) == before, "tag on a 64-bit-size moov fails cleanly")
    r = t.faam_run(["chapter", "import", "wide.m4a", "--chapters", "c1.txt"], d)
    check(r.returncode == 1 and r.stderr and sha(os.path.join(d, "wide.m4a")) == before, "chapter import on a 64-bit-size moov fails cleanly")


def run_same_file(t, d):
    w = os.path.join(d, "sf")
    os.makedirs(w)
    shutil_copy(os.path.join(d, "lc.aac"), os.path.join(w, "in.aac"))
    shutil_copy(os.path.join(d, "lc.aac"), os.path.join(w, "other.aac"))
    shutil_copy(os.path.join(d, "lc.m4a"), os.path.join(w, "in.m4a"))
    shutil_copy(os.path.join(d, "cc64.m4a"), os.path.join(w, "chap.m4a"))
    links = []
    try:
        os.link(os.path.join(w, "in.aac"), os.path.join(w, "hard.aac"))
        links.append("hard.aac")
        os.symlink("in.aac", os.path.join(w, "soft.aac"))
        links.append("soft.aac")
    except (OSError, NotImplementedError, AttributeError):
        print("note: link checks skipped (cannot create links here)")
    original = slurp(os.path.join(d, "lc.aac"))
    for name in ["in.aac", "./in.aac", "../sf/in.aac"] + links:
        for ow in (["--overwrite"], []):
            r = t.faam_run(["-i", "other.aac", "-i", "in.aac", "-o", name] + ow, w)
            check(r.returncode == 1 and r.stderr and slurp(os.path.join(w, "in.aac")) == original,
                  f"mux -o {name} {' '.join(ow)} is an input: refused, input untouched")
    ref = slurp(os.path.join(d, "lc.m4a"))
    for name in ["in.m4a", "./in.m4a"]:
        r = t.faam_run(["demux", "in.m4a", "-o", name, "--overwrite"], w)
        check(r.returncode == 1 and r.stderr and slurp(os.path.join(w, "in.m4a")) == ref, f"demux -o {name} is the input: refused")
        r = t.faam_run(["demux", "in.m4a", "--export-asc", name, "-o", "o.aac", "--overwrite"], w)
        check(r.returncode == 1 and slurp(os.path.join(w, "in.m4a")) == ref and no_output(w, "o.aac"),
              f"demux --export-asc {name} is the input: refused, nothing else written")
    r = t.faam_run(["demux", "in.m4a", "--export-asc", "same.bin", "-o", "same.bin", "--overwrite"], w)
    check(r.returncode == 1 and no_output(w, "same.bin"), "demux -o and --export-asc naming one file is refused")
    chap = slurp(os.path.join(w, "chap.m4a"))
    r = t.faam_run(["chapter", "export", "chap.m4a", "-o", "chap.m4a", "--overwrite"], w)
    check(r.returncode == 1 and r.stderr and slurp(os.path.join(w, "chap.m4a")) == chap, "chapter export -o the input is refused")
    r = t.faam_run(["demux", "in.m4a", "-o", "o2.aac", "--overwrite"], w)
    check(r.returncode == 0, "a different output file is still fine")


def run_large_files(t, d):
    """Offsets past 4 GiB: tag edits and demuxing of files with a co64 table, moov last and moov first."""
    big = (4 << 30) + (512 << 20)
    probe = os.path.join(d, "sparse_probe")
    try:
        with open(probe, "wb") as f:
            f.truncate(big + 4096)
        st = os.stat(probe)
        os.remove(probe)
        sparse = st.st_size == big + 4096 and st.st_blocks * 512 < (64 << 20)
        free = shutil.disk_usage(d).free
    except OSError:
        sparse, free = False, 0
    if not sparse:
        print("note: >4 GiB checks skipped (no sparse files here)")
        return

    src = read(os.path.join(d, "lc_faam.m4a"))
    tops, pos = {}, 0
    while pos + 8 <= len(src):
        size, kind = struct.unpack_from(">I4s", src, pos)
        if size == 1:
            size = struct.unpack_from(">Q", src, pos + 8)[0]
        tops[kind.decode()] = (pos, pos + size)
        pos += size
    ftyp, moov = (src[tops[k][0]:tops[k][1]] for k in ("ftyp", "moov"))
    mdat = src[tops["ftyp"][1]:tops["mdat"][1]]  # includes the placeholder box faam leaves ahead of it
    check(src.index(b"mdat") < src.index(b"moov"), "source has mdat before moov")

    def rebuild(buf, a, b, shift):
        out = b""
        for kind, ca, cb in boxes(buf, a, b):
            if kind in (b"trak", b"mdia", b"minf", b"stbl"):
                inner = rebuild(buf, ca, cb, shift)
                out += struct.pack(">I4s", 8 + len(inner), kind) + inner
            elif kind == b"stco":
                n = struct.unpack_from(">I", buf, ca + 4)[0]
                body = struct.pack(">II", 0, n) + b"".join(
                    struct.pack(">Q", struct.unpack_from(">I", buf, ca + 8 + 4 * i)[0] + shift) for i in range(n))
                out += struct.pack(">I4s", 8 + len(body), b"co64") + body
            else:
                out += buf[ca - 8:cb]
        return out

    def moov_with(shift):
        inner = rebuild(moov, 8, len(moov), shift)
        return struct.pack(">I4s", 8 + len(inner), b"moov") + inner

    def write_big(path, moov_first):
        free_hdr = struct.pack(">I4sQ", 1, b"free", big)
        mv = moov_with(0)
        shift = (len(mv) if moov_first else 0) + big
        mv = moov_with(shift)
        with open(path, "wb") as f:
            f.write(ftyp)
            if moov_first:
                f.write(mv)
            f.write(free_hdr)
            f.seek(big - len(free_hdr), os.SEEK_CUR)
            f.write(mdat)
            if not moov_first:
                f.write(mv)

    ref_aac = os.path.join(d, "big_ref.aac")
    t.faam_run(["demux", "lc_faam.m4a", "-o", ref_aac, "--overwrite"], d)
    for moov_first in (False, True):
        name = "big_first.m4a" if moov_first else "big_last.m4a"
        # moving the tail of a moov-first file turns all of it into real data: gigabytes of disk and
        # a minute of I/O, so it only runs on request
        if moov_first and (not os.environ.get("FAAM_TEST_BIG") or free < 3 * big):
            print("note: moov-first >4 GiB check skipped (set FAAM_TEST_BIG=1 with 15 GB free to run it)")
            continue
        label = "moov first" if moov_first else "moov last"
        path = os.path.join(d, name)
        write_big(path, moov_first)
        r = t.faam_run(["demux", name, "-o", name + ".aac", "--overwrite"], d)
        check(r.returncode == 0 and read(os.path.join(d, name + ".aac")) == read(ref_aac), f"{label}: demux of a file with offsets past 4 GiB ({r.stderr!r})")
        r = t.faam_run(["dump", name], d)
        offsets = [int(x) for x in re.findall(rb"offset=(\d+)", r.stdout)]
        check(r.returncode == 0 and b"[moov]" in r.stdout and max(offsets or [0]) > 1 << 32,
              f"{label}: dump lists a moov past 4 GiB without loading the file")
        r = t.faam_run(["tag", name, "--title", "A title long enough to grow the tag list", "--artist", "Somebody", "--tag", "k=v"], d)
        check(r.returncode == 0, f"{label}: tag edit past 4 GiB (rc={r.returncode} {r.stderr!r})")
        check("Title: A title" in info_tags(t.faam_run(["info", name], d).stdout), f"{label}: tags show after the edit")
        r = t.faam_run(["demux", name, "-o", name + "2.aac", "--overwrite"], d)
        check(r.returncode == 0 and read(os.path.join(d, name + "2.aac")) == read(ref_aac), f"{label}: frames identical after the tag edit")
        r = t.faam_run(["chapter", "import", name, "--chapters", "c1.txt"], d)
        r2 = t.faam_run(["demux", name, "-o", name + "3.aac", "--overwrite"], d)
        check(r.returncode == 0 and r2.returncode == 0 and read(os.path.join(d, name + "3.aac")) == read(ref_aac),
              f"{label}: frames identical after a chapter import")
        for leftover in [path] + [os.path.join(d, name + ext) for ext in (".aac", "2.aac", "3.aac")]:
            if os.path.exists(leftover):
                os.remove(leftover)


SC4 = b"\x00\x00\x00\x01"
SC3 = b"\x00\x00\x01"
SPS264 = b"\x67\x42\x00\x1e\xda\x05\x07\xe8\x40"
PPS264 = b"\x68\xce\x3c\x80"
VPS265 = b"\x40\x01\x0c\x01\xff\xff\x01\x60\x01\x01\x01\x5d"
SPS265 = b"\x42\x01\x01\x01\x60\x01\x01\x01\x90\x01\x01\x01\x01\x01\x5d\xa0\x02\x80\x80"
PPS265 = b"\x44\x01\xc1\x72\xb4\x62\x40"


def nals(items):
    """Annex-B stream of NAL byte strings, alternating 4- and 3-byte start codes."""
    return b"".join((SC4 if i % 2 == 0 else SC3) + n for i, n in enumerate(items))


def video_samples(buf):
    mdat = find(buf, ("mdat",))
    stsz = find(buf, ("moov", "trak", "mdia", "minf", "stbl", "stsz"))
    n = struct.unpack_from(">I", buf, stsz[0] + 8)[0]
    pos, out = mdat[0], []
    for i in range(n):
        size = struct.unpack_from(">I", buf, stsz[0] + 12 + 4 * i)[0]
        out.append(buf[pos:pos + size])
        pos += size
    return out


def sample_nal_types(sample, hevc):
    pos, out = 0, []
    while pos < len(sample):
        n = struct.unpack_from(">I", sample, pos)[0]
        out.append((sample[pos + 4] >> 1) & 0x3F if hevc else sample[pos + 4] & 0x1F)
        pos += 4 + n
    return out


def sync_samples(buf):
    r = find(buf, ("moov", "trak", "mdia", "minf", "stbl", "stss"))
    n = struct.unpack_from(">I", buf, r[0] + 4)[0]
    return [struct.unpack_from(">I", buf, r[0] + 8 + 4 * i)[0] for i in range(n)]


def run_video_packetizer(t, d):
    size = ["--width", "320", "--height", "240"]

    # three hundred thousand NALs the sample buffer was never sized for: three start-code bytes in the
    # file become four length bytes in the sample
    seis = b"".join(b"\x00\x00\x01\x06\x80" for _ in range(300000))
    open(os.path.join(d, "seis.264"), "wb").write(SC4 + SPS264 + SC4 + PPS264 + seis + SC4 + b"\x65\x88" + b"\x55" * 10)
    r = t.faam_run(["-i", "seis.264", "-o", "seis.mp4"] + size, d)
    check(r.returncode == 0, f"a stream of 300000 one-byte NALs muxes (rc={r.returncode})")
    if r.returncode == 0:
        smp = video_samples(read(os.path.join(d, "seis.mp4")))
        check(len(smp) == 1 and sample_nal_types(smp[0], False) == [7, 8] + [6] * 300000 + [5], "all NALs land in the one access unit")

    for codec in ("h264", "h265"):
        hevc = codec == "h265"
        ext = "265" if hevc else "264"
        if hevc:
            ps = [VPS265, SPS265, PPS265]
            sei = b"\x4e\x01\x05\x04\x55\x55\x55\x55\x80"
            suffix = b"\x50\x01\x05\x04\x55\x55\x55\x55\x80"
            aud = b"\x46\x01\x50"
            idr = lambda first: b"\x26\x01" + (b"\x80" if first else b"\x44") + b"\x55\x55"
            trail = lambda first: b"\x02\x01" + (b"\x80" if first else b"\x44") + b"\x55\x55"
            T = {"ps": [32, 33, 34], "sei": 39, "aud": 35, "idr": 19, "trail": 1, "suffix": 40}
        else:
            ps = [SPS264, PPS264]
            sei = b"\x06\x05\x04\x55\x55\x55\x55\x80"
            suffix = None
            aud = b"\x09\xf0"
            idr = lambda first: b"\x65" + (b"\x88" if first else b"\x44") + b"\x55\x55"
            trail = lambda first: b"\x41" + (b"\x9a" if first else b"\x44") + b"\x55\x55"
            T = {"ps": [7, 8], "sei": 6, "aud": 9, "idr": 5, "trail": 1}
        end = [suffix] if suffix else []
        for use_aud in (False, True):
            lead = [aud] if use_aud else []
            lt = [T["aud"]] if use_aud else []
            pictures = [
                lead + ps + [sei, idr(True), idr(False)],
                lead + [trail(True), trail(False)] + end,
                lead + [trail(True)],
                lead + ps + [sei, idr(True)],
                lead + [trail(True), trail(False), trail(False)],
            ]
            expect = [
                lt + T["ps"] + [T["sei"], T["idr"], T["idr"]],
                lt + [T["trail"], T["trail"]] + ([T["suffix"]] if suffix else []),
                lt + [T["trail"]],
                lt + T["ps"] + [T["sei"], T["idr"]],
                lt + [T["trail"]] * 3,
            ]
            name = f"mp_{codec}_{int(use_aud)}"
            open(os.path.join(d, name + "." + ext), "wb").write(nals([n for p in pictures for n in p]))
            r = t.faam_run(["-i", f"{name}.{ext}", "--codec:0", codec, "-o", name + ".mp4"] + size, d)
            check(r.returncode == 0, f"{codec} multi-slice stream muxes (aud={use_aud})")
            if r.returncode != 0:
                continue
            mp4 = read(os.path.join(d, name + ".mp4"))
            got = [sample_nal_types(s, hevc) for s in video_samples(mp4)]
            check(got == expect, f"{codec} aud={use_aud}: one sample per picture, parameter sets and SEI with the next one\n  got    {got}\n  expect {expect}")
            check(sync_samples(mp4) == [1, 4], f"{codec} aud={use_aud}: sync samples are the IDR pictures ({sync_samples(mp4)})")
            r = t.faam_run(["demux", name + ".mp4", "-o", name + "_d." + ext], d)
            r2 = t.faam_run(["-i", f"{name}_d.{ext}", "--codec:0", codec, "-o", name + "_2.mp4"] + size, d)
            check(r.returncode == 0 and r2.returncode == 0 and read(os.path.join(d, name + "_2.mp4")) == mp4,
                  f"{codec} aud={use_aud}: demux and re-mux is stable")

        # parameter sets are required, and must fit
        slice_ = idr(True)
        for what, items in (("no PPS", ps[:-1] + [slice_]), ("no SPS", ps[:-2] + ps[-1:] + [slice_]), ("no parameter sets", [slice_])):
            open(os.path.join(d, "noparam." + ext), "wb").write(nals(items))
            r = t.faam_run(["-i", "noparam." + ext, "--codec:0", codec, "-o", "noparam.mp4", "--overwrite"] + size, d)
            check(r.returncode == 1 and r.stderr and no_output(d, "noparam.mp4"), f"{codec} stream with {what} is an error (rc={r.returncode})")
        if hevc:
            open(os.path.join(d, "novps." + ext), "wb").write(nals([SPS265, PPS265, slice_]))
            r = t.faam_run(["-i", "novps." + ext, "--codec:0", codec, "-o", "novps.mp4"] + size, d)
            check(r.returncode == 1 and no_output(d, "novps.mp4"), "hevc stream without a VPS is an error")
        huge = ps[:]
        huge[-2 if hevc else 0] = huge[-2 if hevc else 0] + b"\x55" * 1500
        open(os.path.join(d, "huge." + ext), "wb").write(nals(huge + [slice_]))
        r = t.faam_run(["-i", "huge." + ext, "--codec:0", codec, "-o", "huge.mp4"] + size, d)
        check(r.returncode == 1 and b"too large" in r.stderr and no_output(d, "huge.mp4"),
              f"{codec} parameter sets over the config size limit are an error ({r.stderr!r})")

    # real encoders, when this machine has them: 4 slices per picture, 50 pictures
    if subprocess.run(["which", "ffmpeg"], capture_output=True).returncode == 0 and \
            subprocess.run(["which", "ffprobe"], capture_output=True).returncode == 0:
        enc = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"], capture_output=True).stdout
        for codec, lib, flag, params in (
                ("h264", b"libx264", "h264", "slices=4:bframes=0:keyint=10:aud=1"),
                ("h264", b"libx264", "h264", "slices=4:bframes=0:keyint=10:aud=0"),
                ("h265", b"libx265", "hevc", "slices=4:bframes=0:keyint=10:aud=1:log-level=error")):
            if lib not in enc:
                continue
            ext = "265" if codec == "h265" else "264"
            pname = "x264-params" if codec == "h264" else "x265-params"
            tag = f"{codec} {params}"
            gen = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc=size=320x240:rate=25", "-frames:v", "50",
                                  "-c:v", lib.decode(), "-" + pname, params, "-f", flag, os.path.join(d, "enc." + ext)], capture_output=True)
            if gen.returncode != 0:
                print("note: ffmpeg could not produce", tag)
                continue
            r = t.faam_run(["-i", "enc." + ext, "--codec:0", codec, "-o", "enc.mp4", "--overwrite", "--width", "320", "--height", "240",
                            "--fps", "25"], d)
            check(r.returncode == 0, f"{tag}: mux of a real encoder stream")
            inf = t.faam_run(["info", "enc.mp4"], d).stdout
            check(b"Total Frames: 50\n" in inf, f"{tag}: faam info counts 50 samples")
            probe = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v", "-show_entries",
                                    "stream=nb_read_frames", "-of", "csv=p=0", os.path.join(d, "enc.mp4")], capture_output=True)
            check(probe.stdout.strip() == b"50", f"{tag}: ffprobe decodes 50 frames ({probe.stdout.strip()!r} {probe.stderr[:100]!r})")


def shutil_copy(a, b):
    with open(a, "rb") as f, open(b, "wb") as g:
        g.write(f.read())


if __name__ == "__main__":
    main()
