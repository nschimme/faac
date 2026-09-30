# H2 confirmation on master: master vs master + freq-scale change at HE 48k (no probe knobs).
export PATH=/opt/venv312/bin:$PATH SWEEP_W=/home/user/lw/h2m_48
S=/home/user/faac/probe/ladder/scripts;M=/home/user/lw/bin/faac_master
until grep -q H2_64_DONE /home/user/lw/h2_run64.log; do sleep 20; done
python $S/s3/sweep.py ctl 48 '' $M; python $S/s3/sweep.py s40 40 '' $M; python $S/s3/sweep.py s56 56 '' $M
python $S/s3/sweep.py pr 48 '' /home/user/lw/bin/faac_pr
echo H2M_DONE
