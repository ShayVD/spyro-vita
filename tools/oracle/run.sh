#!/bin/sh
# Run PCSX-Redux (the behaviour oracle, D8) headless with a Lua script.
# Usage: tools/oracle/run.sh SCRIPT.lua [extra emulator flags...]
# The script quits the emulator (PCSX.quit); if it doesn't (a Lua error, say),
# the run is killed after $ORACLE_TIMEOUT seconds (default 1800). Settings
# and memory cards go to $ORACLE_HOME, by default $ORACLE_OUT/home: a fresh
# card per run, since a card's contents change the title menu.
# The emulator lives in the gitignored tools/oracle/bin/ (see docs/decisions.md D8).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EMU="$ROOT/tools/oracle/bin/PCSX-Redux.app/Contents/MacOS/PCSX-Redux"
SCRIPT=$1; shift
HOME_DIR=${ORACLE_HOME:-${ORACLE_OUT:+$ORACLE_OUT/home}}
HOME_DIR=${HOME_DIR:-$ROOT/tools/oracle/bin/home}
mkdir -p "$HOME_DIR"
exec perl -e 'alarm shift; exec @ARGV or die' "${ORACLE_TIMEOUT:-1800}" "$EMU" -no-ui -testmode -noupdate -portable "$HOME_DIR" \
  -bios "$ROOT/disc/bios/SCPH1001.BIN" -iso "$ROOT/disc/image/Spyro the Dragon (USA).cue" \
  -interpreter -debugger -dofile "$SCRIPT" -run "$@"
