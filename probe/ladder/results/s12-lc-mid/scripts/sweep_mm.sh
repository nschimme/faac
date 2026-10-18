#!/bin/bash
PY=/opt/venv312/bin/python
for mm in pw=0.3 pw=0.6 af=0.0003 af=0.003 pf=0.002 pf=0.01 qb=0.0001 qb=0.001 sm=0.4 sm=0.8 rs=2 rs=4 aw=0.1 aw=0.2 le=0.3 le=0.4 ss=1.0 ss=2.0 \
  "t0=1.5,t1=1.5,t2=1.5" "t0=-1.5,t1=-1.5,t2=-1.5" "t3=1.5,t4=1.5" "t3=-1.5,t4=-1.5" "t5=1.5,t6=1.5,t7=1.5" "t5=-1.5,t6=-1.5,t7=-1.5"; do
  tag=$(echo "$mm" | tr ',=.-' '_pdm')
  FAAC_MM="$mm" $PY var.py mm_$tag gate 2>&1 | grep GATE | sed "s/^/[$mm] /"
done
echo SWEEPDONE
