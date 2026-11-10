#!/bin/sh
# Run PCSX-Redux (the behaviour oracle, D8) headless with a Lua script.
# Usage: [DISC=...] [BIOS=...] tools/oracle/run.sh SCRIPT.lua [extra emulator flags...]
# The script quits the emulator (PCSX.quit); if it doesn't (a Lua error, say),
# the run is killed after $ORACLE_TIMEOUT seconds (default 1800). Settings
# and memory cards go to $ORACLE_HOME, by default $ORACLE_OUT/home: a fresh
# card per run, since a card's contents change the title menu.
# DISC: a .cue, or a folder holding one (default disc/image, the USA disc;
# disc/image-pal and disc/image-jp for the other regions, D132). BIOS: the
# BIOS file (default disc/bios/SCPH1001.BIN). Relative paths are from the
# repo's root.
# The emulator lives in the gitignored tools/oracle/bin/ (see docs/decisions.md
# D8), or where tools/oracle/fetch_redux.sh puts it (.tools/pcsx-redux/).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EMU="$ROOT/tools/oracle/bin/PCSX-Redux.app/Contents/MacOS/PCSX-Redux"
[ -x "$EMU" ] || EMU=$("$ROOT/tools/oracle/fetch_redux.sh")
abs() { case $1 in /*) echo "$1" ;; *) echo "$ROOT/$1" ;; esac; }
DISC=$(abs "${DISC:-disc/image}")
if [ -d "$DISC" ]; then
  CUE=$(ls "$DISC"/*.cue 2>/dev/null | head -1)
  [ -n "$CUE" ] || { echo "run.sh: no .cue in $DISC" >&2; exit 1; }
else
  CUE=$DISC
fi
BIOS=$(abs "${BIOS:-disc/bios/SCPH1001.BIN}")
[ -f "$BIOS" ] || { echo "run.sh: no BIOS at $BIOS" >&2; exit 1; }
SCRIPT=$1; shift
HOME_DIR=${ORACLE_HOME:-${ORACLE_OUT:+$ORACLE_OUT/home}}
HOME_DIR=${HOME_DIR:-$ROOT/tools/oracle/bin/home}
mkdir -p "$HOME_DIR"
echo "run.sh: $CUE, BIOS $BIOS" >&2
exec perl -e 'alarm shift; exec @ARGV or die' "${ORACLE_TIMEOUT:-1800}" "$EMU" -no-ui -testmode -noupdate -portable "$HOME_DIR" \
  -bios "$BIOS" -iso "$CUE" \
  -interpreter -debugger -dofile "$SCRIPT" -run "$@"
