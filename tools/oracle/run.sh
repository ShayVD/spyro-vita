#!/bin/sh
# Run PCSX-Redux (the behaviour oracle, D8) headless with a Lua script.
# Usage: tools/oracle/run.sh SCRIPT.lua [extra emulator flags...]
# The script quits the emulator (PCSX.quit); if it doesn't (a Lua error, say),
# the run is killed after $ORACLE_TIMEOUT seconds (default 1800).
# The emulator lives in the gitignored tools/oracle/bin/ (see docs/decisions.md D8).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EMU="$ROOT/tools/oracle/bin/PCSX-Redux.app/Contents/MacOS/PCSX-Redux"
SCRIPT=$1; shift
exec perl -e 'alarm shift; exec @ARGV or die' "${ORACLE_TIMEOUT:-1800}" "$EMU" -no-ui -testmode -noupdate -portable "${ORACLE_HOME:-$ROOT/tools/oracle/bin/home}" \
  -bios "$ROOT/disc/bios/SCPH1001.BIN" -iso "$ROOT/disc/image/Spyro the Dragon (USA).cue" \
  -interpreter -debugger -dofile "$SCRIPT" -run "$@"
