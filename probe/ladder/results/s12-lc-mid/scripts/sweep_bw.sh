#!/bin/bash
PY=/opt/venv312/bin/python
for c in 17000 20000 20500 22000; do FAAC_EXTRA="-c $c" $PY var.py bw$c gate 2>&1 | grep GATE | sed "s/^/c=$c /"; done
echo SWEEPDONE
