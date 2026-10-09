#!/bin/bash
# usage: udta_ffmpeg.sh <build dir with frontend/faam,faad> [workdir]
# Adds tags and chapters, in both orders, to moov-first/moov-last files from other muxers (ffmpeg, faac)
# that have no udta, and checks decoded PCM, demuxed frames and the reported tags/chapters.
HERE=$(cd "$(dirname "$0")" && pwd); B=$(cd "$1" && pwd); W=${2:-$(mktemp -d)}; mkdir -p "$W"; cd "$W" || exit 1
FAAM=$B/frontend/faam; FAAD=$B/frontend/faad; fail=0
python3 - <<'P'
import wave, struct, math
w = wave.open('in.wav', 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(44100)
w.writeframes(b''.join(struct.pack('<hh', int(9000*math.sin(i*.05)), int(7000*math.sin(i*.031))) for i in range(44100*3))); w.close()
P
printf '00:00:00.000\tIntro\n00:00:01.250\tMiddle\n00:00:02.000\tEnd\n' > ch.txt
ffmpeg -v error -y -i in.wav -c:a aac -movflags +faststart a_fast.m4a
ffmpeg -v error -y -i in.wav -c:a aac -map_metadata -1 -movflags +faststart a_fast_nometa.m4a
ffmpeg -v error -y -i in.wav -c:a aac a_last.m4a
ffmpeg -v error -y -i in.wav -c:a aac -map_metadata -1 a_last_nometa.m4a
ffmpeg -v error -y -i in.wav -c:a aac -map_metadata -1 -fflags +bitexact -flags:a +bitexact bx_last.m4a
ffmpeg -v error -y -i in.wav -c:a aac -map_metadata -1 -fflags +bitexact -flags:a +bitexact -movflags +faststart bx_fast.m4a
for s in a_fast a_last bx_fast bx_last; do python3 "$HERE/strip_udta.py" $s.m4a ${s}_noudta.m4a; done
$B/frontend/faac -q 100 -o plain_faac.m4a in.wav >/dev/null 2>&1
for src in a_fast a_fast_nometa a_last a_last_nometa bx_fast bx_last a_fast_noudta a_last_noudta bx_fast_noudta bx_last_noudta plain_faac; do
  for order in tags_chapters chapters_tags; do
    f=${src}_$order.m4a; cp $src.m4a $f
    rm -f pcm_before.wav; $FAAD -q -o pcm_before.wav $f 2>/dev/null
    ffmpeg -v error -y -i $f -f s16le before.raw
    $FAAM demux -o frames_before.raw $f >/dev/null 2>&1
    echo "[$src $order] udta before: $(ffprobe -v error -show_entries format_tags -of compact $f | head -c 60)"
    if [ $order = tags_chapters ]; then
      $FAAM tag --title "New Title" --artist "New Artist" $f >/dev/null || { echo "tag failed"; fail=1; }
      $FAAM chapter import --chapters ch.txt $f >/dev/null || { echo "chapter failed"; fail=1; }
    else
      $FAAM chapter import --chapters ch.txt $f >/dev/null || { echo "chapter failed"; fail=1; }
      $FAAM tag --title "New Title" --artist "New Artist" $f >/dev/null || { echo "tag failed"; fail=1; }
    fi
    rm -f pcm_after.wav; $FAAD -q -o pcm_after.wav $f 2>/dev/null
    ffmpeg -v error -y -i $f -f s16le after.raw
    $FAAM demux -o frames_after.raw $f >/dev/null 2>&1
    cmp -s before.raw after.raw && cmp -s pcm_before.wav pcm_after.wav && cmp -s frames_before.raw frames_after.raw || { echo "  MEDIA CHANGED"; fail=1; }
    info=$($FAAM info $f 2>&1)
    echo "$info" | grep -q "New Title" && echo "$info" | grep -q "New Artist" && echo "$info" | grep -q "Middle" || { echo "  info missing tags/chapters"; echo "$info" | head -30; fail=1; }
    ffprobe -v error -show_entries format_tags=title,artist -show_chapters -of compact $f | tr '\n' ' ' | grep -q "title=New Title" || { echo "  ffprobe does not see the title"; fail=1; }
    n=$(ffprobe -v error -show_chapters -of csv=p=0 $f | wc -l | tr -d ' '); [ "$n" = 3 ] || { echo "  ffprobe chapters=$n"; fail=1; }
    echo "  ok: pcm $(stat -f %z after.raw 2>/dev/null || stat -c %s after.raw) bytes, ffprobe chapters $n"
  done
done
echo "udta_ffmpeg: fail=$fail"; exit $fail
