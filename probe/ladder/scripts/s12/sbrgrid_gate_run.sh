#!/bin/sh
# usage: sbrgrid_gate_run.sh OUTDIR FAAC CLIPLIST ; evaluation gates: HE-v1 32/40/48, HE-v2 24/32/48 CBR, constants as frozen (env unset = defaults T6 L1)
OUT=$1; ENC=$2; LST=$3
for R in 32 40 48; do nice python3 "$(dirname "$0")/sbrgrid_run.py" "$OUT" "$LST" v1_$R "$ENC" "--object-type he-aac-v1 -b $R --cbr" FAAC_SBR_GRID=1 || exit 1; done
for R in 24 32 48; do nice python3 "$(dirname "$0")/sbrgrid_run.py" "$OUT" "$LST" v2_$R "$ENC" "--object-type he-aac-v2 -b $R --cbr" FAAC_SBR_GRID=1 || exit 1; done
