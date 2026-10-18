#!/bin/bash
PY=/opt/venv312/bin/python
T="t5=-1.5,t6=-1.5,t7=-1.5"
for mm in "pf=0.01,le=0.3" "pf=0.01,le=0.3,aw=0.1" "pf=0.01,le=0.3,aw=0.1,ss=2.0" "pf=0.01,le=0.3,aw=0.1,ss=2.0,$T" "pf=0.01,le=0.3,aw=0.1,ss=2.0,$T,rs=4" \
  "pf=0.02,le=0.25" "pf=0.01,le=0.3,rs=4" "pf=0.02,le=0.3" "pf=0.01,le=0.25" "pf=0.01,le=0.3,ss=2.5"; do
  tag=$(echo "$mm" | tr ',=.-' '_pdm')
  FAAC_MM="$mm" $PY var.py c_$tag gate 2>&1 | grep GATE | sed "s/^/[$mm] /"
done
echo SWEEPDONE
