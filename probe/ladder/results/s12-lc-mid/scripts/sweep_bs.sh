#!/bin/bash
PY=/opt/venv312/bin/python
run(){ tag=$1; shift; env "$@" $PY var.py $tag gate 2>&1 | grep GATE; }
for v in 3 4 6; do run lr$v FAAC_BS_LR=$v; done
for v in 20 50; do run dr$v FAAC_BS_DR=$v; done
for v in 0.15 0.5; do run sm$v FAAC_BS_SM=$v; done
for pn in "1 1" "0 1" "1 2" "2 1" "2 0" "1 0"; do set -- $pn; run pn$1$2 FAAC_BS_PREVS=$1 FAAC_BS_NEXTS=$2; done
echo SWEEPDONE
