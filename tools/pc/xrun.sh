#!/bin/sh
# Run a command under a virtual X display (Xvfb, GL by Mesa's llvmpipe), for
# tools/pc/tc PC_X=1 and CI. xvfb-run's start-up handshake (SIGUSR1) hangs
# under Rosetta, so the server is started here and waited for by its socket.
Xvfb :77 -screen 0 1920x1080x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
xpid=$!
i=0
while [ ! -S /tmp/.X11-unix/X77 ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
DISPLAY=:77 "$@"
rc=$?
kill $xpid 2>/dev/null
exit $rc
