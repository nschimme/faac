# F2 control: score 5 clips of FAAC and fdk (LC 128k) twice; MOS must be identical.
export PATH=/opt/venv312/bin:$PATH FAAC_SF_SMOOTH=0.6 FAAC_BS_DROPRATIO=12
A=/opt/faac-benchmark/data/external/audio;T=/home/user/lw/f2ctl;mkdir -p $T
for c in Severance 21-classic velvet 24-Greensleeves 12-German; do w=$(ls $A/$c*.wav|head -1)
  /home/user/lw/bin/faac_lc -b 128 -o $T/f.m4a "$w" >/dev/null 2>&1; /home/user/lw/bin/fdk_lc -b 128 -o $T/k.m4a "$w"
  for e in f k; do a=$(python /opt/faac-benchmark/scripts/score_clip.py "$w" $T/$e.m4a|grep MOS); b=$(python /opt/faac-benchmark/scripts/score_clip.py "$w" $T/$e.m4a|grep MOS)
  echo "$c $e [$a] [$b] $([ "$a" = "$b" ] && echo SAME || echo DIFF)"; done; rm -f $T/*.m4a; done; rmdir $T
