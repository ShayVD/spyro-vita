#!/bin/sh
# Replay a capture directory (tools/oracle/capture.lua) through the host's
# translated C and summarise. Usage: tools/oracle/replay.sh DIR [--only NAME]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIR=$1; shift
cd "$ROOT"
tools/tc build/linux32/src/platform/linux32/spyro --replay "$DIR" \
  --ignore tools/oracle/ignore_ranges.txt "$@" > "$DIR/replay.txt" || true
python3 tools/oracle/replay_summary.py "$DIR/replay.txt" > "$DIR/summary_replay.txt"
tail -3 "$DIR/summary_replay.txt"
