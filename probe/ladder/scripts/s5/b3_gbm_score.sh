export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12 FAAC_BIN=/home/user/lw/bin/faac_lc LADDER_RATE=128 LADDER_SLOPE=112,144
cd /home/user/faac && python probe/ladder/scripts/s3/arm_score.py /home/user/lw/e128/g b3_gbm F,rGBM,rGBMall
