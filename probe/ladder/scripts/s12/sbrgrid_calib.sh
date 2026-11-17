#!/bin/sh
# usage: sbrgrid_calib.sh OUTDIR FAAC CLIPLIST ; HE-v1 40k CBR, T x L grid, one cache json per (T,L,clip)
OUT=$1; ENC=$2; LST=$3
for T in 2 3 4 6 8; do for L in 0 1 2 3; do
  nice python3 "$(dirname "$0")/sbrgrid_run.py" "$OUT" "$LST" T${T}L${L} "$ENC" "--object-type he-aac-v1 -b 40 --cbr" FAAC_SBR_GRID=1 FAAC_SBR_T=$T FAAC_SBR_L=$L || exit 1
done; done
