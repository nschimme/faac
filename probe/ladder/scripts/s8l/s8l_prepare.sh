#!/usr/bin/env bash
# One serial, restartable prep job. LADDER_CLIP selects one reference stem for debugging.
set -euo pipefail
rate=${1:?rate must be 96 or 128}
case "$rate" in 96) ref=apple_lc96k; slope=80,112;; 128) ref=apple; slope=112,144;; *) exit 2;; esac
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
bench=/Users/nschimme/gitprojects/faac-benchmark
export NUMBA_DISABLE_JIT=1 FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12
export FAAC_BIN="$repo/probe_tmp/bstatic/frontend/faac" FAAD_BIN=/private/tmp/claude-501/faac-work/faad-dump/build-ladder/frontend/faad
export FAAC_BENCHMARK_DATA="$bench/data/external/audio" LADDER_RATE="$rate" LADDER_REF="$ref" LADDER_SLOPE="$slope"
export LADDER_SCORER="$bench/scripts/score_clip.py" LADDER_G="$repo/probe_tmp/s8l/$rate/g"
export PATH="$bench/.venv/bin:$PATH"
test -x "$FAAC_BIN" && test -x "$FAAD_BIN"
"$bench/.venv/bin/python" "$repo/probe/ladder/scripts/g/g2_prepare.py" "$LADDER_G"
