#!/bin/bash
PY=/opt/venv312/bin/python; export FAAC_BIN=/home/user/work/probe-bin/faac_cd
export VAR_CLIPS="SinceAlways.16b48k|take_your_finger_frin_my_head.16b48k|35_SQAM_glockenspiel_cut.16b48k|fms|21-classic.441.16b48k|velvet.16b48k"
run(){ tag=$1; mm=$2; FAAC_MM="$mm" $PY var.py w_$tag gate >/dev/null 2>&1; echo "done $tag"; }
run base ""
run hi3 "t5=3,t6=3,t7=3"
run hi5 "t5=5,t6=5,t7=5"
run mid_m2_hi3 "t3=-2,t4=-2,t5=3,t6=3,t7=3"
run rs2 "rs=2"
run rs2_hi3 "rs=2,t5=3,t6=3,t7=3"
echo WORSTDONE
