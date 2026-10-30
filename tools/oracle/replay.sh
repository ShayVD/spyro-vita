#!/bin/sh
# Replay a capture directory (tools/oracle/capture.lua) through the host's
# translated C and summarise. Usage: tools/oracle/replay.sh DIR [--only NAME]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIR=$1; shift
cd "$ROOT"
BIN=build/linux32/src/platform/linux32/spyro
MAP=build/linux32/oracle/fnmap.txt
if [ ! -f "$MAP" ] || [ "$BIN" -nt "$MAP" ]; then  # PS1 -> host function map
  mkdir -p build/linux32/oracle
  tools/tc sh -c "i686-linux-gnu-nm $BIN > build/linux32/oracle/nm.txt"
  python3 tools/oracle/fnmap.py build/s1-matching/build/psx.map build/linux32/oracle/nm.txt > "$MAP"
fi
tools/tc "$BIN" --replay "$DIR" --ignore tools/oracle/ignore_ranges.txt --fnmap "$MAP" "$@" \
  > "$DIR/replay.txt" || true
python3 tools/oracle/replay_summary.py "$DIR/replay.txt" > "$DIR/summary_replay.txt"
tail -3 "$DIR/summary_replay.txt"
