# B3 prep: G2 dumps + controls at 128k and 96k (s3/e_prep.sh), serially. No scoring here.
S=/home/user/faac/probe/ladder/scripts
bash $S/s3/e_prep.sh 128 apple 112,144 > /home/user/lw/b3_prep128.log 2>&1
bash $S/s3/e_prep.sh 96 apple_lc96k 80,112 > /home/user/lw/b3_prep96.log 2>&1
echo B3PREP_DONE >> /home/user/lw/b3_prep96.log
