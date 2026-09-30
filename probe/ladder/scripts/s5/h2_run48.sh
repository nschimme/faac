# H2 at 48k only (restart: F2's he48 dir was deleted; base/anchors copied from results/s5/f2_he48.json)
export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 SWEEP_W=/home/user/lw/h2_48
S=/home/user/faac/probe/ladder/scripts;B=/home/user/lw/bin/faac_probe
for s in 7 8 9 10 12; do python $S/s3/sweep.py stop$s 48 FAAC_SBR_STOP=$s $B; done
for f in 2 3; do python $S/s3/sweep.py fs$f 48 FAAC_SBR_FREQ_SCALE=$f $B; done
echo H2_DONE
