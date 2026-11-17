#!/bin/bash
# Strict decode of the arm's streams: FAAD2, in-tree FAAD3 --strict, ffmpeg -xerror, fdkdec.
# Criterion per decoder: exit 0 and empty stderr; also decoded PCM byte length equal to master's.
# usage: sbrgrid_strict.sh WORKDIR CLIPLIST FAAC_PATCHED FAAC_MASTER ; needs ENV.sh
W=$1; LST=$2; N=$3; M=$4; mkdir -p "$W"; cd "$W" || exit 1
tot=0; bad=0; diffs=0
run() { # name cmd... ; sets rc_ok
  local name=$1; shift; "$@" >/dev/null 2>err.txt </dev/null; local rc=$?
  if [ $rc -ne 0 ] || [ -s err.txt ]; then echo "FAIL $name rc=$rc $(head -c 120 err.txt)"; return 1; fi; return 0
}
pcmlen() { "$@" 2>/dev/null </dev/null | wc -c; }
while read -r clip; do
  [ -z "$clip" ] && continue
  for cfg in "he-v1 32" "he-v1 40" "he-v1 48" "he-v2 24" "he-v2 32" "he-v2 48"; do
    set -- $cfg; p=$1; r=$2
    FAAC_SBR_GRID=1 nice $N --overwrite --profile $p -b $r --cbr -o g.m4a "$AUDIO/$clip" >/dev/null 2>&1 </dev/null
    nice $M --overwrite --profile $p -b $r --cbr -o m.m4a "$AUDIO/$clip" >/dev/null 2>&1 </dev/null
    tot=$((tot+1)); ok=1
    run "faad2 $clip $cfg"  $FAAD -q -o /dev/null g.m4a || ok=0
    run "faad3 $clip $cfg"  $FAAD3 --strict -q --overwrite -o o3.wav g.m4a || ok=0
    run "ffmpeg $clip $cfg" ffmpeg -nostdin -v error -xerror -i g.m4a -f null - || ok=0
    run "fdk $clip $cfg"    $FDKDEC g.m4a ofdk.wav || ok=0
    [ $ok = 1 ] || bad=$((bad+1))
    # equal decoded length vs master (ffmpeg decode, samples)
    a=$(pcmlen ffmpeg -nostdin -v error -i g.m4a -f s16le -); b=$(pcmlen ffmpeg -nostdin -v error -i m.m4a -f s16le -)
    [ "$a" = "$b" ] || { diffs=$((diffs+1)); echo "LENGTH $clip $cfg $a vs $b"; }
  done
done < "$LST"
echo "streams $tot, decoder failures $bad, length mismatches $diffs"
