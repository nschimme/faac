# usage: e_prep.sh <rate> <ref> <lo,hi>
set -e
export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 FAAC_BIN=${FAAC_BIN:-/home/user/lw/bin/faac_lc} LADDER_RATE=$1 LADDER_REF=$2 LADDER_SLOPE=$3 LADDER_WORK=/home/user/lw/e$1
S=/home/user/faac/probe/ladder/scripts
python $S/g/g2_prepare.py > /home/user/lw/e$1_prep.log
python $S/g/control0.py | tail -1
python $S/s3/e_make.py /home/user/lw/e$1/g | tail -1
python $S/g/g2_controls.py > /home/user/lw/e$1_ctl.log
echo "KA/KF all-pass lines: $(grep -c 'KA True KF True decode True True' /home/user/lw/e$1_ctl.log) / 49; KF_note $(grep -c KF_note /home/user/lw/e$1/g/g2_controls.json)"
cd /home/user/lw/e$1/g && rm -f *.f32 *.aac *_apple.wav *_normal.wav *_c0.bin *_ffmpeg.log *_decode.log
