#!/bin/bash
source /home/user/work/ENV.sh; cd /home/user/work/fb
python3 /home/user/work/runs/cc_faac_only.py --mode encoder --faac-bin /home/user/work/faac-master/build-ps/frontend/faac,/home/user/work/cbrpr/build/frontend/faac \
  --faad-bin $FAAD3 --scenarios 16k_mono_20k,16k_mono_24k,16k_mono_voip_24k,24k_mono_28k,24k_mono_32k,32k_stereo_16k,48k_stereo_16k,48k_stereo_24k,48k_stereo_40k,48k_stereo_80k,48k_stereo_128k,48k_stereo_192k \
  --skip-stereo --skip-transient --skip-graphs --output /home/user/work/cbrv2/leaderboard.md --results-json /home/user/work/cbrv2/results.json
echo ALLDONE
