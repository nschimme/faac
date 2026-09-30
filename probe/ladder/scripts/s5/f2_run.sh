# F2: fdk-aac vs FAAC base per rung, serial, one results file per rung.  usage: f2_run.sh
set -e
export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12
S=/home/user/faac/probe/ladder/scripts
run(){ # rung rate lo hi faacbin fdkbin
  export SWEEP_W=/home/user/lw/f2_$1
  for a in "ctl $2" "s$3 $3" "s$4 $4"; do set -- $1 $2 $3 $4 $5 $6 $a; python $S/s3/sweep.py $7 $8 '' $5; done
  python $S/s3/sweep.py fdk $2 '' $6
}
run he32 32 28 40 /home/user/lw/bin/faac_probe /home/user/lw/bin/fdk_he
run he48 48 40 56 /home/user/lw/bin/faac_probe /home/user/lw/bin/fdk_he
run lc64 64 56 72 /home/user/lw/bin/faac_lc /home/user/lw/bin/fdk_lc
run lc96 96 80 112 /home/user/lw/bin/faac_lc /home/user/lw/bin/fdk_lc
run lc128 128 112 144 /home/user/lw/bin/faac_lc /home/user/lw/bin/fdk_lc
echo F2_DONE
