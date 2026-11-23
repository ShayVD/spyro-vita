#!/bin/sh
# A video of a PC run (docs/pc-build.md): the Linux build under Xvfb with a
# pad script, the X display recorded by ffmpeg from SKIP seconds for SECS
# seconds. In an image FROM spyro-pc with ffmpeg added:
#   tools/pc/record.sh OUT.mp4 SKIP SECS spyro-args...
out=$1 skip=$2 secs=$3
shift 3
Xvfb :78 -screen 0 1280x720x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
xpid=$!
i=0
while [ ! -S /tmp/.X11-unix/X78 ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
DISPLAY=:78 "$@" &
gpid=$!
sleep "$skip"
ffmpeg -loglevel error -y -f x11grab -framerate 60 -video_size 1280x720 -i :78 -t "$secs" \
  -c:v libx264 -preset veryfast -pix_fmt yuv420p -crf 20 "$out"
wait $gpid
rc=$?
kill $xpid 2>/dev/null
exit $rc
