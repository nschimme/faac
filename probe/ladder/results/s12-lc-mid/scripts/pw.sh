#!/bin/bash
PY=/opt/venv312/bin/python; export FAAC_BIN=/home/user/work/probe-bin/faac_cd
FAAC_MM="pw=0.346" $PY var.py pw346 all 2>&1 | grep -E "GATE|ALL49" | sed 's/^/[pw=0.346] /'
for corp in audio audio_44k1; do for r in 64 80; do
  echo "$corp ${r}k pw=0.346: $(FAAC_MM='pw=0.346' $PY exp.py pwlow_${corp}_$r $r $corp -- 2>&1 | tail -1)"
  echo "$corp ${r}k shipped:  $(FAAC_MM='' $PY exp.py pwbase_${corp}_$r $r $corp -- 2>&1 | tail -1)"
done; done
echo PWDONE
