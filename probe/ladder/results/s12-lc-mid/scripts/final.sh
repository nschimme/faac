#!/bin/bash
PY=/opt/venv312/bin/python; export FAAC_BIN=/home/user/work/probe-bin/faac_new
$PY var.py final all 2>&1 | grep -E "GATE|ALL49"
for corp in audio audio_44k1; do for r in 64 80; do echo "$corp ${r}k new: $($PY exp.py finlow_${corp}_$r $r $corp -- 2>&1 | tail -1)"; done; done
echo FINALDONE
