#!/bin/bash
PY=/opt/venv312/bin/python
T="t5=-1.5,t6=-1.5,t7=-1.5"
FAAC_MM="pf=0.01,le=0.3,aw=0.1,ss=2.0,$T,rs=4" $PY var.py g_full all 2>&1 | grep -E "GATE|ALL49" | sed 's/^/[full] /'
FAAC_MM="pf=0.01,le=0.3,ss=2.5" $PY var.py g_min all 2>&1 | grep -E "GATE|ALL49" | sed 's/^/[min] /'
echo GUARDDONE
