#!/usr/bin/env bash
# K0 gates scoring. One serial job owns each per-arm result JSON.
set -euo pipefail
rate=${1:?rate must be 96 or 128}
case "$rate" in 96) slope=80,112;; 128) slope=112,144;; *) exit 2;; esac
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
bench=/Users/nschimme/gitprojects/faac-benchmark
export NUMBA_DISABLE_JIT=1 FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12
export FAAC_BIN="$repo/probe_tmp/bstatic/frontend/faac" LADDER_RATE="$rate" LADDER_SLOPE="$slope"
export LADDER_SCORER="$bench/scripts/score_clip.py" LADDER_G="$repo/probe_tmp/s8l/$rate/g"
export LADDER_RESULTS_DIR="$repo/probe/ladder/results/s8l" PATH="$bench/.venv/bin:$PATH"
python="$bench/.venv/bin/python"
"$python" "$repo/probe/ladder/scripts/s4/k0_ctl.py" "$LADDER_G" K0
for arm in rSFr rSFr0 rSFr1; do
  "$python" "$repo/probe/ladder/scripts/s3/arm_score.py" "$LADDER_G" "${arm}_${rate}k" "$arm"
  "$python" "$repo/probe/ladder/scripts/s3/adj.py" "$LADDER_RESULTS_DIR/${arm}_${rate}k.json" "base${rate}" "$arm"
done
"$python" "$repo/probe/ladder/scripts/s8l/s8l_summary.py" "$rate"
