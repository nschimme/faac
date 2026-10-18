#!/bin/bash
PY=/opt/venv312/bin/python
for fa in "4 16" "4 8" "8 32" "16 32" "16 16" "32 16" "32 8"; do set -- $fa
  FAAC_RC_FRAMES=$1 FAAC_RC_AMORT=$2 $PY var.py rc$1_$2 gate 2>&1 | grep GATE | sed "s/^/F=$1 A=$2 /"; done
echo SWEEPDONE
