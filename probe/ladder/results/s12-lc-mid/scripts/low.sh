#!/bin/bash
PY=/opt/venv312/bin/python; export FAAC_BIN=/home/user/work/probe-bin/faac_bs
T="t5=-1.5,t6=-1.5,t7=-1.5"
FULL="pf=0.01,le=0.3,aw=0.1,ss=2.0,$T,rs=4"; MIN="pf=0.01,le=0.3,ss=2.5"
for corp in audio audio_44k1; do for r in 64 80; do
  echo "$corp ${r}k base: $($PY exp.py lowb_${corp}_$r $r $corp -- 2>&1 | tail -1)"
  echo "$corp ${r}k full: $(FAAC_MM="$FULL" $PY exp.py lowf_${corp}_$r $r $corp -- 2>&1 | tail -1)"
  echo "$corp ${r}k min:  $(FAAC_MM="$MIN" $PY exp.py lowm_${corp}_$r $r $corp -- 2>&1 | tail -1)"
done; done
echo LOWDONE
