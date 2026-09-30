# B3 offline screen: step1 arms at 128k then 96k, serial scorer, one results file per rate.
export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 FAAC_BIN=/home/user/lw/bin/faac_lc
cd /home/user/faac
LADDER_RATE=128 LADDER_SLOPE=112,144 python probe/ladder/scripts/s3/arm_score.py /home/user/lw/e128/g b3_arms F,rSF06,rFIT,rFITall
LADDER_RATE=96 LADDER_SLOPE=80,112 python probe/ladder/scripts/s3/arm_score.py /home/user/lw/e96/g b3_arms F,rSF06,rFIT,rFITall
echo B3_SCORE_DONE
