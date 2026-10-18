#!/bin/bash
r=$1; w=$2; n=$(basename "$w" .wav); d=/home/user/work/an/d/$r; mkdir -p "$d"
F=/home/user/work/lc-mid/b/frontend/faac; D=/tmp/faad-dump/bf/frontend/faad
A="/home/user/faac/probe/ladder/ref/apple-s11-367cf8dc/lc/${r}k/$n.wav.m4a"
[ -f "$A" ] || { echo "MISSING $A" >&2; exit 0; }
"$F" -b $r --object-type lc -o "$d/$n.faac.m4a" "$w" >/dev/null 2>"$d/$n.faac.log"
ffmpeg -v error -y -i "$d/$n.faac.m4a" -c copy -f adts "$d/$n.faac.aac"
ffmpeg -v error -y -i "$A" -c copy -f adts "$d/$n.apple.aac"
for e in faac apple; do rm -f "$d/$n.$e.dump"; FAAD_DUMP="$d/$n.$e.dump" "$D" -o "$d/$n.$e.wav" --overwrite "$d/$n.$e.aac" >/dev/null 2>&1; rm -f "$d/$n.$e.wav"; done
echo "$r $n $(stat -c%s "$d/$n.faac.aac") $(stat -c%s "$d/$n.apple.aac")"
