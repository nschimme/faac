# usage: e_score.sh <rate> <lo,hi>
export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 FAAC_BIN=/home/user/lw/bin/faac_lc LADDER_RATE=$1 LADDER_SLOPE=$2
cd /home/user/faac && python probe/ladder/scripts/s3/arm_score.py /home/user/lw/e$1/g e_arms F,A,rSF,rWIN,H_W
