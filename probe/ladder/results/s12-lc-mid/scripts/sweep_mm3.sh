#!/bin/bash
PY=/opt/venv312/bin/python
for mm in "pf=0.01,le=0.3,ss=3.0" "pf=0.01,le=0.3,ss=3.5" "pf=0.012,le=0.3,ss=2.5" "pf=0.008,le=0.3,ss=2.5" "pf=0.01,le=0.32,ss=2.5" "pf=0.01,le=0.28,ss=2.5" "pf=0.01,le=0.3,ss=2.5,aw=0.1" "pf=0.01,le=0.3,ss=3.0,rs=4"; do
  tag=$(echo "$mm" | tr ',=.-' '_pdm')
  FAAC_MM="$mm" $PY var.py d_$tag gate 2>&1 | grep GATE | sed "s/^/[$mm] /"
done
echo SWEEPDONE
