#!/usr/bin/env bash
# Install the Vita build in Vita3K, run it until it exits (config.txt's
# frames=) or a timeout, and collect its log and screenshots.
#   tools/vita/vita3k_run.sh OUT_DIR [TIMEOUT_S] [config line]...
#   tools/vita/vita3k_run.sh logs/run1-vita-title 120 shot_every=240 frames=2400
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
mkdir -p "$out"
: > "$data/config.txt"
for line in "$@"; do echo "$line" >> "$data/config.txt"; done
rm -rf "$data/shots" "$data/spyro.log"

# A .vpk argument installs (over an older install) and runs it.
"$vita3k" --frontend sdl -l 4 "$vpk" > "$out/vita3k.txt" 2>&1 &
pid=$!
t=0
while [ $t -lt "$timeout" ] && kill -0 $pid 2>/dev/null; do
  grep -q 'reached, exiting' "$data/spyro.log" 2>/dev/null && break
  sleep 2; t=$((t + 2))
done
sleep 1
kill $pid 2>/dev/null || true
cp "$data/config.txt" "$out/config.txt"
cp "$data/spyro.log" "$out/spyro.log" 2>/dev/null || echo "no spyro.log"
[ -d "$data/shots" ] && cp "$data/shots/"*.png "$out/" 2>/dev/null || true
echo "vita3k_run: ${t}s, $(ls "$out" | grep -c '\.png$') shots in $out"
tail -3 "$out/spyro.log" 2>/dev/null || true
