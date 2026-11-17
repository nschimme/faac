#!/bin/bash
# Payload identity matrix: master vs patched with FAAC_SBR_GRID unset (all modes) and, for LC, set.
# usage: sbrgrid_matrix.sh WORKDIR MASTER_FAAC PATCHED_FAAC ; prints per-case md5 equality and totals.
# Inputs: 10 s excerpts of three clips at 48000/44100/32000 Hz, stereo and mono.
W=$1; M=$2; N=$3; mkdir -p "$W"; cd "$W" || exit 1
for c in "01 castanets" "07 applaud" "09 Linchpin"; do for sr in 48000 44100 32000; do for ch in 2 1; do
  f="$(echo "$c" | tr ' ' '_')_${sr}_${ch}.wav"
  [ -f "$f" ] || ffmpeg -v error -y -t 10 -i "$AUDIO/$c.wav" -ar $sr -ac $ch -c:a pcm_s16le "$f"
done; done; done
md5() { ffmpeg -v error -i "$1" -map 0:a -c copy -f md5 - 2>&1; }
cfgs=("--profile lc -b 96" "--profile lc -b 96 --cbr" "--profile lc -q 100"
      "--profile he-v1 -b 24" "--profile he-v1 -b 32" "--profile he-v1 -b 48" "--profile he-v1 -b 64"
      "--profile he-v1 -b 24 --cbr" "--profile he-v1 -b 32 --cbr" "--profile he-v1 -b 48 --cbr" "--profile he-v1 -b 64 --cbr"
      "--profile he-v1 -q 20" "--profile he-v1 -q 60"
      "--profile he-v2 -b 24 --cbr" "--profile he-v2 -b 32 --cbr" "--profile he-v2 -b 48 --cbr")
total=0; same=0; lcset=0; lcsame=0
for f in *_*_*.wav; do
  ch=${f##*_}; ch=${ch%.wav}
  for cfg in "${cfgs[@]}"; do
    case "$cfg" in *he-aac-v2*) [ "$ch" = 1 ] && continue;; esac
    $M --overwrite $cfg -o m.m4a "$f" >/dev/null 2>&1; rm=$?
    $N --overwrite $cfg -o u.m4a "$f" >/dev/null 2>&1; ru=$?
    total=$((total+1))
    if [ $rm = $ru ] && [ "$(md5 m.m4a)" = "$(md5 u.m4a)" ]; then same=$((same+1)); else echo "DIFF unset: $f $cfg"; fi
    case "$cfg" in *lc*)
      FAAC_SBR_GRID=1 $N --overwrite $cfg -o s.m4a "$f" >/dev/null 2>&1
      lcset=$((lcset+1)); [ "$(md5 m.m4a)" = "$(md5 s.m4a)" ] && lcsame=$((lcsame+1)) || echo "DIFF LC set: $f $cfg";;
    esac
  done
done
echo "unset == master: $same / $total"; echo "LC with FAAC_SBR_GRID=1 == master: $lcsame / $lcset"
