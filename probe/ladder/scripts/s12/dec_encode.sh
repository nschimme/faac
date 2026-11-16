#!/bin/bash
# Encode FAAC master variants for decision-dump study (single process).
source /home/user/work/ENV.sh
O=/home/user/work/dec_streams
run() { # name args...
  local name=$1; shift
  mkdir -p "$O/$name"
  for f in "$AUDIO"/*.wav; do
    b=$(basename "$f")
    nice -n 10 $FAAC_MASTER --overwrite "$@" -o "$O/$name/$b.m4a" "$f" >/dev/null 2>&1 || echo "FAIL $name $b"
  done
}
for r in 96 128 192; do
  run abr$r --object-type lc -b $r
  run ms$r --object-type lc -b $r --joint ms
done
run vbr160 --object-type lc -q 160
run vbr342 --object-type lc -q 342
run he32 --object-type he-aac-v1 -b 32
run he48 --object-type he-aac-v1 -b 48
echo DONE
