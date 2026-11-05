#!/usr/bin/env bash
# hw_run.sh and hw_check.sh against hw_mock.py (a pretend Vita on localhost), for vitacompanion 1.08 and 1.05:
# a run to frames= with a pad script and owner files that must come back untouched, a hang with a crash dump,
# and --restore after a run killed with SIGKILL. Needs python3, unzip, zip.
#   vita/tools/test/hw_run_test.sh
set -euo pipefail
tools=$(cd "$(dirname "$0")/.." && pwd)
w=$(mktemp -d "${TMPDIR:-/tmp}/hw_run_test.XXXXXX")
trap 'rc=$?; kill $(jobs -p) 2> /dev/null || true; [ "${KEEP:-0}" = 1 ] || rm -rf "$w"; exit $rc' EXIT
fails=0
check() { if eval "$2"; then echo "  ok   $1"; else echo "  FAIL $1"; fails=$((fails + 1)); fi; }

# a fake game: its game file, and a VPK with an eboot.bin and sce_sys/
mkdir -p "$w/game/tools/vita" "$w/vpk/sce_sys"
cat > "$w/game/tools/vita/hw.game" <<'GAME'
HW_TITLE_ID=TEST00001
HW_NAME="Test game"
HW_DATA=ux0:data/testgame
HW_LOG=test.log
HW_STOP_RE='frames: stop'
HW_HANG_RE='hang: stopping the app'
HW_VPK=test.vpk
HW_TIMEOUT=60
HW_POLL_S=1
HW_PAD_NAME=pad.txt
HW_NAMES=(save)
HW_COLLECT=(save)
HW_SAVE_DIR=save
hw_after_setup() { echo "after_setup $1" > "$w_marker"; }
GAME
echo 'eboot' > "$w/vpk/eboot.bin"; echo 'sfo' > "$w/vpk/sce_sys/param.sfo"
(cd "$w/vpk" && zip -qr "$w/game/test.vpk" .)
export HW_GAME=$w/game/tools/vita/hw.game HW_STATE=$w/state VITA_IP=127.0.0.1 w_marker=$w/marker

port=$((20000 + RANDOM % 20000))
start_mock() { # VERSION
  MOCK_VERSION=$1 python3 -I "$tools/hw_mock.py" "$w/vita-$1" $port $((port + 1)) > "$w/mock-$1.log" 2>&1 &
  mock=$!
  export VITA_FTP_PORT=$port VITA_CMD_PORT=$((port + 1))
  for _ in $(seq 50); do python3 -c "import socket; socket.create_connection(('127.0.0.1', $port), 1)" 2> /dev/null && return; sleep 0.1; done
  echo "the mock did not start"; exit 1
}

for ver in 1.08 1.05; do
  echo "vitacompanion $ver:"
  start_mock $ver
  root=$w/vita-$ver
  data=$root/ux0/data/testgame
  mkdir -p "$data/save"
  printf 'volume=40\npad=ux0:data/testgame/old.txt\n' > "$data/config.txt"
  echo "owner log" > "$data/test.log"
  echo "owner card" > "$data/save/card0"
  if [ $ver = 1.05 ]; then # no promote on 1.05: installed once by hand
    mkdir -p "$root/ux0/app/TEST00001" && echo old > "$root/ux0/app/TEST00001/eboot.bin"
  fi
  "$tools/hw_check.sh" > "$w/check-$ver.txt" 2>&1 || true
  check "hw_check reaches it" "grep -q 'hw_check: ready' '$w/check-$ver.txt'"

  echo "1 - CROSS" > "$w/pad.txt"
  rc=0; PAD=$w/pad.txt "$tools/hw_run.sh" "$w/out-$ver" 30 frames=1200 extra=1 > "$w/run-$ver.txt" 2>&1 || rc=$?
  check "the run reaches frames=" "[ $rc = 0 ] && grep -q 'frames= reached' '$w/out-$ver/hw-run.txt'"
  check "the run's config: the owner's lines but pad=, then the arguments, then pad=" \
    "[ \"\$(tr '\n' ' ' < '$w/out-$ver/config.txt')\" = 'volume=40  frames=1200 extra=1 pad=ux0:data/testgame/pad.txt ' ]"
  check "the log and the save folder fetched" "grep -q 'frames: stop' '$w/out-$ver/test.log' && [ -d '$w/out-$ver/save' ]"
  check "the eboot.bin installed" "[ \"\$(cat '$root/ux0/app/TEST00001/eboot.bin')\" = eboot ]"
  check "the owner's files back as they were" "grep -q '^volume=40' '$data/config.txt' && [ \"\$(cat '$data/test.log')\" = 'owner log' ] && [ \"\$(cat '$data/save/card0')\" = 'owner card' ] && [ ! -e '$data/pad.txt' ] && ! ls '$data' | grep -q hw-"
  check "the hw_after_setup hook ran" "grep -q after_setup '$w/marker'"
  check "the lock is gone" "[ ! -e '$w/state/hw.lock' ]"

  rc=0; "$tools/hw_run.sh" "$w/hang-$ver" 30 hang_test=600 > "$w/hang-$ver.txt" 2>&1 || rc=$?
  check "a hang stops the run, with its crash dump" "[ $rc != 0 ] && grep -q 'stopped after .*: hang' '$w/hang-$ver/hw-run.txt' && ls '$w/hang-$ver' | grep -q psp2core"
  rm -f "$root"/ux0/data/psp2core-*

  "$tools/hw_run.sh" "$w/kill-$ver" 30 frames=100000 > "$w/kill-$ver.txt" 2>&1 & r=$!
  for _ in $(seq 100); do [ -e "$data/config.txt.hw-backup" ] && grep -q launch "$w/kill-$ver/hw-run.txt" 2>/dev/null && break; sleep 0.1; done
  kill -9 $r; wait $r 2>/dev/null || true
  python3 -I "$tools/hw_ftp.py" cmd "$( [ $ver = 1.05 ] && echo kill || echo quit ) TEST00001" > /dev/null 2>&1 || true
  check "a killed run leaves .hw-backup files" "[ -e '$data/config.txt.hw-backup' ]"
  "$tools/hw_run.sh" --restore > "$w/restore-$ver.txt" 2>&1 || true
  check "--restore puts them back" "grep -q '^volume=40' '$data/config.txt' && [ ! -e '$data/config.txt.hw-backup' ] && [ \"\$(cat '$data/save/card0')\" = 'owner card' ]"

  check "hw_ftp refuses writes outside the game's folders" "! python3 -I '$tools/hw_ftp.py' put '$w/pad.txt' ux0:data/other.txt 2> /dev/null"
  kill $mock; wait $mock 2>/dev/null || true
done
[ $fails = 0 ] && echo "hw_run_test: ok" || { echo "hw_run_test: $fails failed (logs in $w)"; trap - EXIT; exit 1; }
