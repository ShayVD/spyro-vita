#!/usr/bin/env bash
# Check that hw_run.sh can reach the Vita: vitacompanion's FTP (port 1337) and command port (1338), the game
# installed, its data folder, leftovers of a killed run, crash dumps, and whatever the game file's hw_check_game
# hook adds (the disc, the GPU's shader compiler ...). It only reads: nothing on the Vita changes (the command
# port is asked "version" and "nosleep status"). From spyro-vita tools/vita/hw_check.sh @ e365217.
#   HW_GAME=tools/vita/hw.game hw_check.sh
# VITA_IP, or the game's tools/vita/hw.env (gitignored; VITA_IP=192.168.1.23).
# While hw_run.sh's lock ($HW_STATE/hw.lock) holds a live PID, another run has the Vita: it says "in use by PID N
# (GAME): not probing" and exits 0 without a packet to the Vita. HW_CHECK_FORCE=1 probes anyway.
set -uo pipefail
kit=$(cd "$(dirname "$0")" && pwd)
: "${HW_GAME:?set HW_GAME to the hw.game file of the game (vita/tools/hw.game.example)}"
HW_GAME=$(cd "$(dirname "$HW_GAME")" && pwd)/$(basename "$HW_GAME")
groot=$(git -C "$(dirname "$HW_GAME")" rev-parse --show-toplevel 2>/dev/null || (cd "$(dirname "$HW_GAME")/../.." && pwd)) # the game file is ROOT/tools/vita/hw.game
HW_NAME=
# shellcheck source=/dev/null
. "$HW_GAME"
export HW_GAME HW_TITLE_ID HW_DATA
# shellcheck source=/dev/null
[ -z "${VITA_IP:-}" ] && [ -f "$groot/tools/vita/hw.env" ] && . "$groot/tools/vita/hw.env" # VITA_IP given wins
: "${VITA_IP:?set VITA_IP or write it in tools/vita/hw.env}"
export VITA_IP VITA_FTP_PORT VITA_CMD_PORT
name=${HW_NAME:-$HW_TITLE_ID}
v() { python3 -I "$kit/hw_ftp.py" "$@"; }
bad=0
ok() { echo "  ok    $*"; }
no() { echo "  FAIL  $*"; bad=1; }
note() { echo "  note  $*"; }

echo "hw_check: the Vita at $VITA_IP, for $name"
if [ "${HW_CHECK_FORCE:-0}" = 1 ]; then
  export HW_IGNORE_LOCK=1 # hw_ftp.py would refuse while the lock is held
else
  busy=$(v lock); [ $? = 3 ] && { echo "hw_check: $busy: not probing (HW_CHECK_FORCE=1 probes anyway)"; exit 0; }
fi
case $(uname) in Darwin|*BSD) wait_s="-t 4" ;; *) wait_s="-W 4" ;; esac # -t is Linux's TTL
if l=$(v ls ux0: 2>&1); then
  ok "FTP on port ${VITA_FTP_PORT:-1337}: ux0: has $(wc -l <<< "$l" | tr -d ' ') entries"
else
  no "FTP on port ${VITA_FTP_PORT:-1337}: $l"
  # shellcheck disable=SC2086
  if ! ping -c 2 $wait_s "$VITA_IP" > /dev/null 2>&1; then
    echo "hw_check: no answer to ping at $VITA_IP: the Vita is asleep or off (press PS to wake it), or this isn't its address"
  elif grep -q 'refused' <<< "$l"; then
    echo "hw_check: $VITA_IP answers ping but nothing listens on ${VITA_FTP_PORT:-1337}: vitacompanion isn't loaded, or this address is another device"
  else
    echo "hw_check: $VITA_IP answers ping but not on ${VITA_FTP_PORT:-1337}: is vitacompanion loaded?"
  fi
  exit 1
fi
help=$(v cmd help 2>&1)
has() { awk -v c="$1" '$1 == c { f = 1 } END { exit !f }' <<< "$help"; }
if has quit || has kill; then
  if has version; then r=$(v cmd version 2>&1); else r="1.05-style (no version command)"; fi
  ok "command port ${VITA_CMD_PORT:-1338}: vitacompanion $r; stops the app with $(has quit && echo quit || echo kill); $(has promote && echo "installs it with promote" || echo "no promote: install the VPK once with VitaShell")"
  r=$(v cmd nosleep status 2>&1)
  case $r in
    "No-sleep enabled.") ok "no standby while vitacompanion runs (nosleep on)" ;;
    *) note "\"nosleep status\" -> $r: the Vita can go to standby between runs (send \"nosleep on\" or reboot)" ;;
  esac
else
  no "command port ${VITA_CMD_PORT:-1338}: \"help\" -> $help (hw_run.sh needs vitacompanion 1.05 or later)"
fi
if s=$(v size "ux0:app/$HW_TITLE_ID/eboot.bin" 2>/dev/null); then
  ok "$name installed (ux0:app/$HW_TITLE_ID/eboot.bin, $s bytes)"
else
  if has promote; then note "$name isn't installed: hw_run.sh installs it (promote)"; else no "$name isn't installed: install its VPK once with VitaShell"; fi
fi
if d=$(v ls "$HW_DATA" 2>/dev/null); then
  ok "$HW_DATA is there ($(wc -l <<< "$d" | tr -d ' ') entries)"
  left=$(grep -E '\.hw-(backup|left-[0-9-]+)$' <<< "$d" | sed 's/^[df] [0-9]* //' | tr '\n' ' ')
  [ -z "$left" ] || note "left by a run that couldn't finish: $left(hw_run.sh --restore puts .hw-backup back)"
else
  no "no $HW_DATA folder"
fi
! declare -F hw_check_game > /dev/null || hw_check_game
n=$(v ls ux0:data 2>/dev/null | grep -c ' psp2core-.*\.psp2dmp$')
[ "$n" = 0 ] || note "$n crash dump(s) in ux0:data (psp2core-*.psp2dmp); hw_run.sh only fetches new ones"
[ $bad = 0 ] && echo "hw_check: ready" || echo "hw_check: not ready"
exit $bad
