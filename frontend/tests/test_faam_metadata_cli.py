"""
FAAM - Freeware Advanced Audio Muxer
Copyright (C) 2026 Nils Schimmelmann

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
"""

"""Independent ilst wire checks and CLI integration; no third-party packages."""
import pathlib
import os
import struct
import subprocess
import sys
import tempfile

FAAM, FIXTURE = sys.argv[1:3]


def run(*args, ok=True):
    p = subprocess.run([FAAM, *map(str, args)], capture_output=True)
    assert (p.returncode == 0) == ok, (args, p.stdout, p.stderr)
    return p.stdout.decode("utf-8", errors="replace")


def boxes(data, start=0, end=None):
    end = len(data) if end is None else end
    while start < end:
        size, key = struct.unpack_from(">I4s", data, start)
        assert size >= 8 and start + size <= end, (start, size, end)
        yield key, start, size
        start += size
    assert start == end


def path(data):
    result = []
    start, end = 0, len(data)
    for key in (b"moov", b"udta", b"meta", b"ilst"):
        _, off, size = next(b for b in boxes(data, start, end) if b[0] == key)
        result.append((off, size))
        start, end = off + (12 if key == b"meta" else 8), off + size
    return result


def children(data):
    off, size = path(data)[-1]
    return [(k, data[p:p+n]) for k, p, n in boxes(data, off + 8, off + size)]


def values(data, key):
    out = []
    for k, raw in children(data):
        if k != key:
            continue
        for typ, p, n in boxes(raw, 8):
            if typ == b"data":
                data_type, locale = struct.unpack_from(">II", raw, p + 8)
                out.append((data_type, locale, raw[p + 16:p + n]))
    return out


def atom(key, content=b""):
    return struct.pack(">I4s", 8 + len(content), key) + content


def data_value(value, typ=1, locale=0):
    return atom(b"data", struct.pack(">II", typ, locale) + value)


def append_children(data, extra):
    parents = path(data)
    off, size = parents[-1]
    pos = off + size
    data = bytearray(data[:pos] + extra + data[pos:])
    for off, size in parents:
        struct.pack_into(">I", data, off, size + len(extra))
    return bytes(data)


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    song = root / "song.m4a"
    subprocess.run([FIXTURE, "--fixture", str(song)], check=True)
    tag_help = " ".join(run("--help-tag").split())
    for option in ("--lyrics <text>", "--grouping <text>", "--copyright <text>",
                   "--description <text>", "--long-description <text>", "--work <text>",
                   "--movement <text>", "--bpm <0-65535>", "--movement-number <0-65535>",
                   "--movement-total <0-65535>", "--show-work-movement", "--lyrics-file <file>"):
        assert option in tag_help, option
    assert "No timing information is added" in tag_help
    assert "optional UTF-8 BOM" in tag_help
    assert "Accepts an integer from 0 through 65535" in tag_help

    before = song.read_bytes()
    preserved = [(k, v) for k, v in children(before) if k != b"\xa9lyr"]
    # Include opaque atoms and multiple data values inside a single parent.
    foreign = atom(b"zzzz", b"opaque\x00\xff") + atom(b"desc", data_value(b"one", locale=7) + data_value(b"two", locale=8))
    song.write_bytes(append_children(before, foreign))
    preserved += [(k, raw) for k, raw in children(song.read_bytes()) if k in (b"zzzz", b"desc")]
    # The CLI stores lyrics with canonical LF line endings on every host.
    lyrics = "  Première ligne\n日本語 🎵\n\nlast line\n"
    source = root / "lyrics.txt"
    source.write_bytes(b"\xef\xbb\xbf" + lyrics.encode())
    run("tag", song, "--lyrics-file", source)
    after = song.read_bytes()
    assert values(after, b"\xa9lyr") == [(1, 0, lyrics.encode())]
    assert [(k, v) for k, v in children(after) if k != b"\xa9lyr"] == preserved
    info = run("info", song)
    assert f"Lyrics:{os.linesep}      Première ligne{os.linesep}    日本語 🎵" in info
    assert "Description: one" in info and "Description: two" in info

    # iTunes may store CR-only line breaks. The CLI normalizes them to LF in the
    # metadata atom, then renders them with host-native line endings.
    cr_only_lyrics = b"First line\rSecond line\r"
    source.write_bytes(cr_only_lyrics)
    run("tag", song, "--lyrics-file", source)
    normalized_lyrics = b"First line\nSecond line\n"
    assert values(song.read_bytes(), b"\xa9lyr") == [(1, 0, normalized_lyrics)]
    info = run("info", song)
    assert f"Lyrics:{os.linesep}    First line{os.linesep}    Second line{os.linesep}" in info

    run("tag", song, "--lyrics", "First line\r\nSecond line\rThird line\n")
    assert values(song.read_bytes(), b"\xa9lyr") == [
        (1, 0, b"First line\nSecond line\nThird line\n")]

    run("tag", song, "--comment", "Comment one\r\nComment two\rComment three\n")
    assert values(song.read_bytes(), b"\xa9cmt") == [
        (1, 0, b"Comment one\nComment two\nComment three\n")]
    run("tag", song, "--remove", "comment")

    # All aliases use the generic serializer, with independent type/width checks.
    text_options = {"grouping": b"\xa9grp", "copyright": b"cprt", "description": b"desc",
                    "long-description": b"ldes", "work": b"\xa9wrk", "movement": b"\xa9mvn"}
    for name, key in text_options.items():
        run("tag", song, "--" + name, "text " + name)
        assert values(song.read_bytes(), key) == [(1, 0, ("text " + name).encode())]
        run("tag", song, "--remove", name)
        assert not values(song.read_bytes(), key), (name, "--remove")
    for name, key in (("bpm", b"tmpo"), ("movement-number", b"\xa9mvi"), ("movement-total", b"\xa9mvc")):
        run("tag", song, "--" + name, "65535")
        assert values(song.read_bytes(), key) == [(21, 0, b"\xff\xff")]
        for bad in ("-1", "65536", "2.5", "no"):
            unchanged = song.read_bytes()
            run("tag", song, "--" + name, bad, ok=False)
            assert song.read_bytes() == unchanged
        run("tag", song, "--remove", name)
        assert not values(song.read_bytes(), key), (name, "--remove")
    run("tag", song, "--show-work-movement")
    assert values(song.read_bytes(), b"shwm") == [(21, 0, b"\x01")]
    run("tag", song, "--remove", "show-work-movement")
    assert not values(song.read_bytes(), b"shwm")

    # Reject invalid UTF-8, including overlong encodings, surrogates and NUL.
    for bad in (b"a\x00b", b"\xff", b"\xc0\xaf", b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"\xf0\x9f"):
        source.write_bytes(bad)
        unchanged = song.read_bytes()
        run("tag", song, "--lyrics-file", source, ok=False)
        assert song.read_bytes() == unchanged
    run("tag", song, "--lyrics-file", root / "missing", ok=False)
    source.write_bytes(b"")
    run("tag", song, "--lyrics-file", source)
    assert values(song.read_bytes(), b"\xa9lyr") == [(1, 0, b"")]
    run("tag", song, "--lyrics", "first", "--remove", "lyrics", "--lyrics", "  last\n")
    assert values(song.read_bytes(), b"\xa9lyr") == [(1, 0, b"  last\n")]
    run("tag", song, "--remove", "lyrics")
    assert not values(song.read_bytes(), b"\xa9lyr")
    # Exercise every short-shrink padding case before the next edit.
    for length in (100, 99, 97, 94, 90, 85, 79, 72):
        run("tag", song, "--lyrics", "x" * length)
        assert values(song.read_bytes(), b"\xa9lyr") == [(1, 0, b"x" * length)]
    source.write_text("x" * 100000 + "\n", encoding="utf-8")
    run("tag", song, "--lyrics-file", source)
    assert values(song.read_bytes(), b"\xa9lyr")[0][2] == ("x" * 100000 + "\n").encode()
    run("tag", song, "--title", "Title", "--artist", "Artist", "--track", "3/12", "--disc", "1/2", "--genre", "Jazz", "--compilation")
    # A genre is the text atom alone; setting it removes a numeric atom the file already has.
    assert values(song.read_bytes(), b"\xa9gen") == [(1, 0, b"Jazz")] and not values(song.read_bytes(), b"gnre")
    song.write_bytes(append_children(song.read_bytes(), atom(b"gnre", data_value(b"\0\x12", 0))))
    assert "Genre Code: 18" in run("info", song)
    run("tag", song, "--genre", "Jazz")
    assert values(song.read_bytes(), b"\xa9gen") == [(1, 0, b"Jazz")] and not values(song.read_bytes(), b"gnre")
    assert "Genre Code" not in run("info", song)
    before = song.read_bytes()
    for bad in ("192", "255", "256"):
        run("tag", song, "--genre", bad, ok=False)
    assert song.read_bytes() == before
    run("tag", song, "--genre", "191")
    assert values(song.read_bytes(), b"\xa9gen") == [(1, 0, b"Psybient")]
    run("tag", song, "--genre", "Not A Genre")
    assert values(song.read_bytes(), b"\xa9gen") == [(1, 0, b"Not A Genre")] and not values(song.read_bytes(), b"gnre")
    song.write_bytes(append_children(song.read_bytes(), atom(b"gnre", data_value(b"\0\x12", 0))))
    run("tag", song, "--remove", "genre")
    assert not values(song.read_bytes(), b"\xa9gen") and not values(song.read_bytes(), b"gnre")
    run("tag", song, "--genre", "Jazz")
    run("tag", song, "--track", "4", "--disc", "2")
    assert values(song.read_bytes(), b"trkn")[0][2] == b"\0\0\0\4\0\x0c\0\0"
    assert values(song.read_bytes(), b"disk")[0][2] == b"\0\0\0\2\0\2\0\0"
    run("tag", song, "--remove", "track", "--track", "5")
    assert values(song.read_bytes(), b"trkn")[0][2][4:6] == b"\0\0"
    run("tag", song, "--tag", "MusicBrainz Track Id=abc", "--tag", "MusicBrainz Track Id=def",
        "--tag", "Multiline=First\r\nSecond\rThird")
    assert (1, 0, b"First\nSecond\nThird") in values(song.read_bytes(), b"----")
    info = run("info", song)
    assert "MusicBrainz Track Id: def" in info
    run("tag", song, "--remove", "custom")
    assert "MusicBrainz Track Id" not in run("info", song)
    assert "iTunSMPB" in run("info", song)
    run("tag", song, "--clear", "--lyrics", "Only lyrics")
    assert [k for k, _ in children(song.read_bytes())] == [b"\xa9lyr"]

    # All options also work while muxing; demux and remux the fixture's payload.
    adts = root / "input.aac"
    run("demux", song, "-o", adts)
    output = root / "mux.m4a"
    run("-i", adts, "-o", output,
        "--lyrics", "Mux lyrics\n", "--grouping", "Group", "--copyright", "Copyright",
        "--description", "Description", "--long-description", "Long description",
        "--work", "Work", "--movement", "Movement", "--bpm", "120",
        "--movement-number", "3", "--movement-total", "4", "--show-work-movement")
    muxed = output.read_bytes()
    assert values(muxed, b"\xa9lyr") == [(1, 0, b"Mux lyrics\n")]
    for key, text in ((b"\xa9grp", "Group"), (b"cprt", "Copyright"), (b"desc", "Description"),
                       (b"ldes", "Long description"), (b"\xa9wrk", "Work"), (b"\xa9mvn", "Movement")):
        assert values(muxed, key) == [(1, 0, text.encode())], key
    for key, value in ((b"tmpo", 120), (b"\xa9mvi", 3), (b"\xa9mvc", 4)):
        assert values(muxed, key) == [(21, 0, value.to_bytes(2, "big"))], key
    assert values(muxed, b"shwm") == [(21, 0, b"\x01")]

    # Reject broken ilst lengths without discarding the malformed tail.
    subprocess.run([FIXTURE, "--fixture", str(song)], check=True)
    broken = append_children(song.read_bytes(), struct.pack(">I4s", 100, b"bad!"))
    song.write_bytes(broken)
    run("tag", song, "--lyrics", "new", ok=False)
    assert song.read_bytes() == broken

print("metadata CLI and wire-format checks passed")
