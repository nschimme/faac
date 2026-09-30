# H2 follow-up (not in the rule): freq scale 3 in the rest of the FINE tier, HE 64k (auto), raw + bytes vs base.
export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 SWEEP_W=/home/user/lw/h2_64
S=/home/user/faac/probe/ladder/scripts;B=/home/user/lw/bin/faac_probe
python $S/s3/sweep.py ctl 64 '' $B; python $S/s3/sweep.py fs3 64 FAAC_SBR_FREQ_SCALE=3 $B
echo H2_64_DONE
