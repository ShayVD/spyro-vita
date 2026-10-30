#!/usr/bin/env bash
# Install the Vita build in Vita3K, run it until it exits (config.txt's
# frames=) or a timeout, and collect its log and screenshots.
#   tools/vita/vita3k_run.sh OUT_DIR [TIMEOUT_S] [config line]...
#   tools/vita/vita3k_run.sh logs/run1-vita-title 120 shot_every=240 frames=2400
# PAD=FILE plays a pad script (linux32 --pad format; adds pad=); SAVE=DIR
# starts from a copy of that memory-card folder (card0, ...); the cards the
# run leaves and audio.wav (wav=1) are copied to OUT_DIR too. REALTIME=1
# runs with the device's real-time vblanks instead of virtual ones.
# VITA3K_LOG=N sets Vita3K's log level (default 4, ERROR) through a copy of
# its config.yml in OUT_DIR; the user's own config is left as it is.
# One run at a time: every run shares Vita3K's ux0:data/spyro and title
# SPYV00001, so a lock (ux0:data/spyro.lock, holding the run's PID) makes a
# second run wait (up to VITA3K_LOCK_WAIT seconds, default 1800). Vita3K is
# always stopped and reaped when the script ends, however it ends (timeout,
# frames=, error, Ctrl-C, SIGTERM, SIGHUP); only SIGKILL can leave it behind.
# Run the script in the foreground (or wait for it): don't detach it from the
# shell that started it.
# Needs Vita3K (macOS app) with firmware installed, and the extracted disc
# files in Vita3K's ux0:data/spyro (docs/vita-testing.md). Vita3K is not a
# Vita: what runs here still has to be checked on hardware.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
out=$1; shift
timeout=${1:-120}; [ $# -gt 0 ] && shift
vita3k=${VITA3K:-/Applications/Vita3K.app/Contents/MacOS/Vita3K}
data="$HOME/Library/Application Support/Vita3K/Vita3K/fs/ux0/data/spyro"
vpk="$root/build/vita/spyro-vita.vpk"

[ -f "$data/manifest.json" ] || { echo "no $data/manifest.json"; exit 1; }

pid=
lock="$data.lock"
stop_vita3k() {
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
    kill -9 "$pid" 2>/dev/null || true
  fi
  [ -n "$pid" ] && wait "$pid" 2>/dev/null || true
  pid=
}
cleanup() {
  stop_vita3k
  [ "$(cat "$lock/pid" 2>/dev/null)" = "$$" ] && rm -rf "$lock"
  return 0
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

waited=0
until mkdir "$lock" 2>/dev/null; do
  owner=$(cat "$lock/pid" 2>/dev/null || true)
  if [ -n "$owner" ] && ! kill -0 "$owner" 2>/dev/null; then
    rm -rf "$lock"; continue # left by a run that was SIGKILLed
  fi
  [ $waited -ge "${VITA3K_LOCK_WAIT:-1800}" ] && { echo "vita3k_run: $lock held by PID $owner"; exit 1; }
  [ $waited = 0 ] && echo "vita3k_run: waiting for the run with PID $owner"
  sleep 5; waited=$((waited + 5))
done
echo $$ > "$lock/pid"
mkdir -p "$out"
# Virtual vblanks (realtime=0) so runs replay exactly and compare with the
# host; REALTIME=1 keeps the device default (vblanks follow the display).
: > "$data/config.txt"
[ "${REALTIME:-0}" = 1 ] || echo "realtime=0" >> "$data/config.txt"
# Scripted runs compare with the host: the original 4:3, 30 fps picture
# unless a config line given here turns widescreen or interp on (later
# lines win).
printf 'widescreen=0\ninterp=0\n' >> "$data/config.txt"
for line in "$@"; do echo "$line" >> "$data/config.txt"; done
rm -rf "$data/shots" "$data/spyro.log" "$data/audio.wav" "$data/save"
mkdir -p "$data/save"
if [ -n "${PAD:-}" ]; then
  cp "$PAD" "$data/pad.txt"; cp "$PAD" "$out/pad.txt"
  echo "pad=ux0:data/spyro/pad.txt" >> "$data/config.txt"
fi
[ -n "${SAVE:-}" ] && cp -R "$SAVE/." "$data/save/"

cfg="$HOME/Library/Application Support/Vita3K/Vita3K/config.yml"
sed "s/^log-level: .*/log-level: ${VITA3K_LOG:-4}/" "$cfg" > "$out/vita3k-config.yml"

# A .vpk argument installs (over an older install) and runs it.
"$vita3k" --frontend sdl -c "$out/vita3k-config.yml" -w "$vpk" > "$out/vita3k.txt" 2>&1 &
pid=$!
t=0
while [ $t -lt "$timeout" ] && kill -0 $pid 2>/dev/null; do
  grep -q 'reached, exiting' "$data/spyro.log" 2>/dev/null && break
  sleep 2; t=$((t + 2))
done
sleep 1
stop_vita3k
cp "$data/config.txt" "$out/config.txt"
cp "$data/spyro.log" "$out/spyro.log" 2>/dev/null || echo "no spyro.log"
[ -d "$data/shots" ] && cp "$data/shots/"*.png "$out/" 2>/dev/null || true
[ -f "$data/audio.wav" ] && cp "$data/audio.wav" "$out/"
rm -rf "$out/save"; cp -R "$data/save" "$out/save"
echo "vita3k_run: ${t}s, $(ls "$out" | grep -c '\.png$') shots in $out"
tail -3 "$out/spyro.log" 2>/dev/null || true
