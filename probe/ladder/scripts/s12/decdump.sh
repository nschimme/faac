#!/bin/sh
# usage: decdump.sh <stream.m4a|.aac|.adts> <dumpfile>
# Decodes with the instrumented libfaad (FAAD_DECDUMP) and discards PCM.
# Override binary with DECDUMP_BIN. Falls back to ffmpeg remux to ADTS.
set -e
BIN=${DECDUMP_BIN:-/home/user/work/faad-dump/build-rel/frontend/faad}
in=$1; out=$2
[ -n "$in" ] && [ -n "$out" ] || { echo "usage: $0 <stream> <dumpfile>" >&2; exit 2; }
rm -f "$out"
if ! FAAD_DECDUMP="$out" "$BIN" -w -f raw "$in" >/dev/null 2>&1 || [ ! -s "$out" ]; then
  t=$(mktemp --suffix=.aac)
  ffmpeg -v error -y -i "$in" -c:a copy -f adts "$t"
  FAAD_DECDUMP="$out" "$BIN" -w -f raw "$t" >/dev/null 2>&1
  rm -f "$t"
fi
[ -s "$out" ] || { echo "empty dump for $in" >&2; exit 1; }
