#!/bin/sh
# Cross-checks libfaam's fragmented and Annex-B output against ffmpeg/ffprobe.
# Not part of `meson test`: skipped (exit 0) when the tools are missing.
# Usage: tests/faam_fragmented_ffprobe.sh [build-dir]
set -e
build=${1:-build}
tool=$build/libfaam/test_faam_media

if ! command -v ffmpeg >/dev/null 2>&1 || ! command -v ffprobe >/dev/null 2>&1; then
    echo "SKIP: ffmpeg/ffprobe not found"; exit 0
fi
if [ ! -x "$tool" ]; then
    echo "SKIP: $tool missing (build with muxer-video enabled)"; exit 0
fi

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

# Real H.264 with an AUD in front of every access unit; 90 frames, a keyframe per second.
if ! ffmpeg -v error -y -f lavfi -i testsrc=size=160x120:rate=30 -t 3 -c:v libx264 -g 30 -bf 0 \
        -x264-params aud=1 -f h264 "$dir/in.h264" 2>/dev/null; then
    echo "SKIP: ffmpeg cannot encode H.264 here"; exit 0
fi

packets() { ffprobe -v quiet -count_packets -select_streams v -show_entries stream=nb_read_packets -of csv=p=0 "$1"; }
keys() { ffprobe -v quiet -select_streams v -show_entries packet=flags -of csv=p=0 "$1" | grep -c '^K'; }
fail() { echo "FAIL: $*"; exit 1; }

# 0: progressive with avcC derived from the stream, 1000: fragmented once per second.
for ms in 0 1000; do
    out=$dir/out_$ms.mp4
    frames=$("$tool" --mux-h264 "$dir/in.h264" "$out" $ms)
    errors=$(ffmpeg -v error -i "$out" -f null - 2>&1 || true)
    [ -z "$errors" ] || fail "fragment_ms=$ms: ffmpeg decode reported: $errors"
    [ "$(packets "$out")" = "$frames" ] || fail "fragment_ms=$ms: packet count $(packets "$out") != $frames"
    [ "$(keys "$out")" = 3 ] || fail "fragment_ms=$ms: keyframes $(keys "$out") != 3"
    echo "ok: fragment_ms=$ms, $frames frames, 3 keyframes, clean decode"
done

# A recording cut off mid-write keeps its completed fragments.
size=$(wc -c < "$dir/out_1000.mp4")
head -c $((size * 60 / 100)) "$dir/out_1000.mp4" > "$dir/cut.mp4"
got=$(packets "$dir/cut.mp4")
[ -n "$got" ] && [ "$got" -ge 30 ] && [ "$got" -lt 90 ] || fail "truncated file demuxes $got frames"
echo "ok: truncated at 60%, $got frames recovered"
