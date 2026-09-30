# H2: SBR stop / freq-scale sweep at the base crossover, HE 32k and 48k. Chained after F2 (reuses a COPY of F2's
# he32/he48 ctl + slope anchors; separate results file per rate).
set -e
export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12
S=/home/user/faac/probe/ladder/scripts;B=/home/user/lw/bin/faac_probe
until grep -q F2_DONE /home/user/lw/f2_run.log; do sleep 30; done
for r in 32 48; do
  export SWEEP_W=/home/user/lw/h2_$r;mkdir -p $SWEEP_W
  python3 -c "import json;r=json.load(open('/home/user/lw/f2_he$r/results.json'));json.dump({k:v for k,v in r.items() if k!='fdk'},open('$SWEEP_W/results.json','w'),indent=0)"
  for s in 7 8 9 10 12; do python $S/s3/sweep.py stop$s $r FAAC_SBR_STOP=$s $B; done
  if [ $r = 32 ]; then fs="1 2"; else fs="2 3"; fi
  for f in $fs; do python $S/s3/sweep.py fs$f $r FAAC_SBR_FREQ_SCALE=$f $B; done
  [ $r = 32 ] && python $S/s4/ref_score.py /home/user/faac/probe/ladder/ref/apple_he32k apple
done
echo H2_DONE
