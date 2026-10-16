#!/bin/bash
# center 10 s clips, stereo s16, at given rate: mkcorpus.sh RATE OUTDIR
RATE=$1; OUT=$2; mkdir -p "$OUT"
cd /home/user/data
find PMLT2014 SoundExpert -name "*.wav" | grep -E "48k.wav$|SoundExpert" | grep -vE "48k\.[0-9]+\.wav$" | while read -r f; do
  d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")
  ss=$(python3 -c "d=float('$d');print(max(0,(d-10)/2) if d>10 else 0)")
  n=$(basename "$f" | tr ' ,' '__')
  ffmpeg -nostdin -v error -y -ss $ss -t 10 -i "$f" -ar $RATE -ac 2 -sample_fmt s16 "$OUT/$n"
done
