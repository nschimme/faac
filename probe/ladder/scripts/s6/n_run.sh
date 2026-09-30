# usage: n_run.sh <rate> <lo> <hi> <lowstart>
set -e
export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 SWEEP_W=/home/user/lw/n$1
[ $1 = 48 ] && export FAAC_SBR_FREQ_SCALE=3
S=/home/user/faac/probe/ladder/scripts/s3;B=/home/user/lw/bin/faac_probe
python $S/sweep.py ctl $1 '' $B; python $S/sweep.py s$2 $2 '' $B; python $S/sweep.py s$3 $3 '' $B
python $S/sweep.py L $1 FAAC_SBR_START=$4 $B
for x in "" "FAAC_SBR_START=$4,"; do t=$([ -z "$x" ] && echo h || echo l)
  for a in i0:FAAC_SBR_INVF=0 i1:FAAC_SBR_INVF=1 i2:FAAC_SBR_INVF=2 n6:FAAC_SBR_NOISE=6 n8:FAAC_SBR_NOISE=8 n10:FAAC_SBR_NOISE=10 n14:FAAC_SBR_NOISE=14 i1n8:FAAC_SBR_INVF=1,FAAC_SBR_NOISE=8 i1n10:FAAC_SBR_INVF=1,FAAC_SBR_NOISE=10; do
    python $S/sweep.py $t${a%%:*} $1 "$x${a#*:}" $B; done; done
echo N$1 DONE
