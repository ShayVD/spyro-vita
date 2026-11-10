#!/usr/bin/env bash
# hw_run.sh and hw_check.sh against hw_mock.py (a pretend Vita on localhost), for vitacompanion 1.08 and 1.05:
# a run to frames= with a pad script and owner files that must come back untouched, the build's SHA-1s in the
# run's account, a hang with a crash dump (its ELF kept, HW_KEEP_APP putting the eboot.bin back), --restore
# after a run killed with SIGKILL and after a cut-off eboot.bin upload, --put-eboot, and the lock: hw_ftp.py
# and hw_check.sh while another live PID holds it, a lock wait that times out, a stale lock. Needs python3,
# unzip, zip, shasum.
#   vita/tools/test/hw_run_test.sh
set -euo pipefail
tools=$(cd "$(dirname "$0")/.." && pwd)
w=$(cd "$(mktemp -d "${TMPDIR:-/tmp}/hw_run_test.XXXXXX")" && pwd) # no // (a TMPDIR ending in /)
trap 'rc=$?; kill $(jobs -p) 2> /dev/null || true; [ "${KEEP:-0}" = 1 ] || rm -rf "$w"; exit $rc' EXIT
fails=0
check() { if eval "$2"; then echo "  ok   $1"; else echo "  FAIL $1"; fails=$((fails + 1)); fi; }
sha() { shasum < "$1" | cut -c1-40; }
ftp() { python3 -I "$tools/hw_ftp.py" "$@"; }

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
echo 'elf' > "$w/game/test.elf"
# for --put-eboot: a VPK, a bare eboot.bin, a VPK without one
mkdir -p "$w/put/sce_sys" "$w/noeboot/sce_sys"
echo 'put eboot' > "$w/put/eboot.bin"; echo 'sfo' > "$w/put/sce_sys/param.sfo"
(cd "$w/put" && zip -qr "$w/put.vpk" .)
echo 'raw eboot' > "$w/raw-eboot.bin"
echo 'sfo' > "$w/noeboot/sce_sys/param.sfo"
(cd "$w/noeboot" && zip -qr "$w/noeboot.vpk" .)
export HW_GAME=$w/game/tools/vita/hw.game HW_STATE=$w/state VITA_IP=127.0.0.1 w_marker=$w/marker

port=$((20000 + RANDOM % 20000))
start_mock() { # VERSION [NAME]
  MOCK_VERSION=$1 python3 -I "$tools/hw_mock.py" "$w/vita-${2:-$1}" $port $((port + 1)) > "$w/mock-${2:-$1}.log" 2>&1 &
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
  appd=$root/ux0/app/TEST00001
  mkdir -p "$data/save"
  printf 'volume=40\npad=ux0:data/testgame/old.txt\n' > "$data/config.txt"
  echo "owner log" > "$data/test.log"
  echo "owner card" > "$data/save/card0"
  if [ $ver = 1.05 ]; then # no promote on 1.05: installed once by hand
    mkdir -p "$appd" && echo old > "$appd/eboot.bin"
  fi
  "$tools/hw_check.sh" > "$w/check-$ver.txt" 2>&1 || true
  check "hw_check reaches it" "grep -q 'hw_check: ready' '$w/check-$ver.txt'"

  echo "1 - CROSS" > "$w/pad.txt"
  rc=0; PAD=$w/pad.txt "$tools/hw_run.sh" "$w/out-$ver" 30 frames=1200 extra=1 > "$w/run-$ver.txt" 2>&1 || rc=$?
  check "the run reaches frames=" "[ $rc = 0 ] && grep -q 'frames= reached' '$w/out-$ver/hw-run.txt'"
  check "the run's config: the owner's lines but pad=, then the arguments, then pad=" \
    "[ \"\$(tr '\n' ' ' < '$w/out-$ver/config.txt')\" = 'volume=40  frames=1200 extra=1 pad=ux0:data/testgame/pad.txt ' ]"
  check "the log and the save folder fetched" "grep -q 'frames: stop' '$w/out-$ver/test.log' && [ -d '$w/out-$ver/save' ]"
  check "the eboot.bin installed, no eboot.bin.hw-new left" "[ \"\$(cat '$appd/eboot.bin')\" = eboot ] && [ ! -e '$appd/eboot.bin.hw-new' ]"
  check "hw-run.txt: the eboot's and the VPK's SHA-1s and the VPK's path, after the old prefix" \
    "grep -qF 'app: eboot.bin (6 bytes, test.vpk, ?, eboot SHA-1 $(sha "$w/vpk/eboot.bin"), VPK $w/game/test.vpk, VPK SHA-1 $(sha "$w/game/test.vpk"))' '$w/out-$ver/hw-run.txt'"
  check "hw-run.env: VPK= VPK_SHA1= EBOOT_SHA1= HEAD=" \
    "(. '$w/out-$ver/hw-run.env' && [ \"\$VPK\" = '$w/game/test.vpk' ] && [ \"\$VPK_SHA1\" = $(sha "$w/game/test.vpk") ] && [ \"\$EBOOT_SHA1\" = $(sha "$w/vpk/eboot.bin") ] && [ -z \"\$HEAD\" ])"
  check "the owner's files back as they were" "grep -q '^volume=40' '$data/config.txt' && [ \"\$(cat '$data/test.log')\" = 'owner log' ] && [ \"\$(cat '$data/save/card0')\" = 'owner card' ] && [ ! -e '$data/pad.txt' ] && ! ls '$data' | grep -q hw-"
  check "the hw_after_setup hook ran" "grep -q after_setup '$w/marker'"
  check "the lock is gone" "[ ! -e '$w/state/hw.lock' ]"

  echo before > "$appd/eboot.bin" # the build that was on the Vita
  rc=0; HW_KEEP_APP=1 HW_ELF=test.elf "$tools/hw_run.sh" "$w/hang-$ver" 30 hang_test=600 > "$w/hang-$ver.txt" 2>&1 || rc=$?
  check "a hang stops the run, with its crash dump" "[ $rc != 0 ] && grep -q 'stopped after .*: hang' '$w/hang-$ver/hw-run.txt' && ls '$w/hang-$ver' | grep -q psp2core"
  check "HW_ELF: the ELF copied next to the dump, its SHA-1 in hw-run.txt" \
    "cmp -s '$w/game/test.elf' \"\$(ls '$w/hang-$ver'/psp2core-*.elf)\" && grep -q 'its ELF: psp2core-.*\.elf, .*SHA-1 $(sha "$w/game/test.elf")' '$w/hang-$ver/hw-run.txt'"
  check "HW_KEEP_APP: the eboot.bin from before fetched, and back after the hang, read back identical" \
    "[ \"\$(cat '$w/hang-$ver/eboot-before.bin')\" = before ] && [ \"\$(cat '$appd/eboot.bin')\" = before ] && grep -q 'READBACK IDENTICAL' '$w/hang-$ver/hw-run.txt' && [ ! -e '$appd/eboot.bin.hw-new' ]"
  rm -f "$root"/ux0/data/psp2core-*

  "$tools/hw_run.sh" "$w/kill-$ver" 30 frames=100000 > "$w/kill-$ver.txt" 2>&1 & r=$!
  for _ in $(seq 100); do [ -e "$data/config.txt.hw-backup" ] && grep -q launch "$w/kill-$ver/hw-run.txt" 2>/dev/null && break; sleep 0.1; done
  kill -9 $r; wait $r 2>/dev/null || true
  ftp cmd "$( [ $ver = 1.05 ] && echo kill || echo quit ) TEST00001" > /dev/null 2>&1 || true
  check "a killed run leaves .hw-backup files" "[ -e '$data/config.txt.hw-backup' ]"
  "$tools/hw_run.sh" --restore > "$w/restore-$ver.txt" 2>&1 || true
  check "--restore puts them back" "grep -q '^volume=40' '$data/config.txt' && [ ! -e '$data/config.txt.hw-backup' ] && [ \"\$(cat '$data/save/card0')\" = 'owner card' ]"
  # an eboot.bin upload cut off between deleting eboot.bin and the rename, then one cut off before the delete
  echo cutoff > "$appd/eboot.bin.hw-new"; rm "$appd/eboot.bin"
  "$tools/hw_run.sh" --restore > "$w/restore2-$ver.txt" 2>&1 || true
  check "--restore finishes an eboot.bin.hw-new left without its eboot.bin" "[ \"\$(cat '$appd/eboot.bin')\" = cutoff ] && [ ! -e '$appd/eboot.bin.hw-new' ]"
  echo partial > "$appd/eboot.bin.hw-new"
  "$tools/hw_run.sh" --restore > "$w/restore3-$ver.txt" 2>&1 || true
  check "--restore deletes an eboot.bin.hw-new beside an eboot.bin" "[ \"\$(cat '$appd/eboot.bin')\" = cutoff ] && [ ! -e '$appd/eboot.bin.hw-new' ]"

  rc=0; "$tools/hw_run.sh" --put-eboot "$w/put.vpk" "$w/put-$ver" > "$w/put-$ver.txt" 2>&1 || rc=$?
  check "--put-eboot VPK: installed, both SHA-1s, READBACK IDENTICAL" \
    "[ $rc = 0 ] && [ \"\$(cat '$appd/eboot.bin')\" = 'put eboot' ] && [ \$(grep -c 'SHA-1 $(sha "$w/put/eboot.bin")' '$w/put-$ver/hw-run.txt') = 2 ] && grep -q 'READBACK IDENTICAL' '$w/put-$ver/hw-run.txt' && [ ! -e '$appd/eboot.bin.hw-new' ] && [ ! -e '$w/state/hw.lock' ]"
  rc=0; "$tools/hw_run.sh" --put-eboot "$w/raw-eboot.bin" > "$w/put-raw-$ver.txt" 2>&1 || rc=$?
  check "--put-eboot eboot.bin (account in \$HW_STATE/put-eboot-TITLEID)" \
    "[ $rc = 0 ] && [ \"\$(cat '$appd/eboot.bin')\" = 'raw eboot' ] && grep -q 'READBACK IDENTICAL' '$w/state/put-eboot-TEST00001/hw-run.txt'"
  rc=0; "$tools/hw_run.sh" --put-eboot "$w/noeboot.vpk" "$w/put-bad-$ver" > "$w/put-bad-$ver.txt" 2>&1 || rc=$?
  check "--put-eboot refuses a VPK without an eboot.bin, nothing changed" \
    "[ $rc != 0 ] && grep -q 'no eboot.bin in' '$w/put-bad-$ver/hw-run.txt' && [ \"\$(cat '$appd/eboot.bin')\" = 'raw eboot' ]"

  check "hw_ftp refuses writes outside the game's folders" "! ftp put '$w/pad.txt' ux0:data/other.txt 2> /dev/null"
  echo a > "$data/a"; echo b > "$data/b"
  check "the mock, like the Vita, won't rename onto a name that is there" "! ftp mv ux0:data/testgame/a ux0:data/testgame/b 2> /dev/null && [ \"\$(cat '$data/b')\" = b ]"
  kill $mock; wait $mock 2>/dev/null || true
done

# The lock: another run (a PID started here) holds the Vita.
echo "the lock:"
start_mock 1.08 lock
appd=$w/vita-lock/ux0/app/TEST00001
mkdir -p "$w/vita-lock/ux0/data/testgame" "$appd"; echo old > "$appd/eboot.bin"
sleep 600 & holder=$!
mkdir -p "$w/state/hw.lock"; echo $holder > "$w/state/hw.lock/pid"; echo OTHER0001 > "$w/state/hw.lock/game"
rc=0; ftp ls ux0:data > /dev/null 2> "$w/lock-ftp.txt" || rc=$?
check "hw_ftp refuses while another live PID holds the lock (exit 3)" "[ $rc = 3 ] && grep -q 'the Vita is in use by PID $holder (OTHER0001)' '$w/lock-ftp.txt'"
rc=0; ftp cmd version > /dev/null 2>&1 || rc=$?
check "... commands too" "[ $rc = 3 ]"
check "HW_IGNORE_LOCK=1 lets it through" "HW_IGNORE_LOCK=1 ftp ls ux0:data > /dev/null"
check "the lock's holder passes (HW_LOCK_HELD)" "HW_LOCK_HELD=$holder ftp ls ux0:data > /dev/null"
cat > "$w/hold.sh" <<'HOLD'
echo $$ > "$1/pid"
python3 -I "$2" ls ux0:data > /dev/null; rc=$?
echo "$3" > "$1/pid"
exit $rc
HOLD
check "a script that holds the lock itself passes (an ancestor of the call)" "bash '$w/hold.sh' '$w/state/hw.lock' '$tools/hw_ftp.py' $holder"
rc=0; VITA_FTP_PORT=1 VITA_CMD_PORT=1 "$tools/hw_check.sh" > "$w/check-lock.txt" 2>&1 || rc=$? # probing would fail
check "hw_check doesn't probe while the lock is held, exits 0" "[ $rc = 0 ] && grep -q 'in use by PID $holder (OTHER0001): not probing' '$w/check-lock.txt'"
rc=0; HW_CHECK_FORCE=1 "$tools/hw_check.sh" > "$w/check-force.txt" 2>&1 || rc=$?
check "HW_CHECK_FORCE=1 probes anyway" "[ $rc = 0 ] && grep -q 'hw_check: ready' '$w/check-force.txt'"
rc=0; HW_LOCK_WAIT=0 "$tools/hw_run.sh" "$w/lock-run" 30 frames=1200 > "$w/lock-run.txt" 2>&1 || rc=$?
check "a lock wait that times out: exit 1, 'waiting' and 'NOT RUN' in hw-run.txt, the Vita untouched" \
  "[ $rc = 1 ] && grep -q '^hw_run: waiting for the run with PID $holder (OTHER0001)' '$w/lock-run/hw-run.txt' && grep -q '^hw_run: NOT RUN: .* held by PID $holder' '$w/lock-run/hw-run.txt' && [ \"\$(cat '$appd/eboot.bin')\" = old ] && [ \"\$(cat '$w/state/hw.lock/pid')\" = $holder ]"
kill $holder; wait $holder 2>/dev/null || true
check "a stale lock (dead PID) doesn't block hw_ftp" "ftp ls ux0:data > /dev/null"
rc=0; "$tools/hw_check.sh" > "$w/check-stale.txt" 2>&1 || rc=$?
check "nor hw_check" "grep -q 'hw_check: ready' '$w/check-stale.txt'"
sleep 600 & holder=$!
echo $holder > "$w/state/hw.lock/pid"
rc=0; HW_LOCK_WAIT=60 "$tools/hw_run.sh" --put-eboot "$w/put.vpk" "$w/lock-put" > "$w/lock-put.txt" 2>&1 & r=$!
for _ in $(seq 50); do grep -q waiting "$w/lock-put/hw-run.txt" 2>/dev/null && break; sleep 0.1; done
kill $holder; wait $holder 2>/dev/null || true
wait $r || rc=$?
check "--put-eboot waits for the lock like a run, then installs" \
  "[ $rc = 0 ] && grep -q 'waiting for the run with PID $holder' '$w/lock-put/hw-run.txt' && grep -q 'READBACK IDENTICAL' '$w/lock-put/hw-run.txt' && [ \"\$(cat '$appd/eboot.bin')\" = 'put eboot' ] && [ ! -e '$w/state/hw.lock' ]"
kill $mock; wait $mock 2>/dev/null || true

# A game that is a git checkout (every real one): its tools/vita/hw.env gives VITA_IP, HW_VPK is found from the
# game's root, and the run's account names the commit.
echo "a game in a git checkout:"
cp -R "$w/game" "$w/ggame"
git -C "$w/ggame" init -q
git -C "$w/ggame" -c user.name=test -c user.email=test@example.invalid commit -q --allow-empty -m test
echo "VITA_IP=127.0.0.1" > "$w/ggame/tools/vita/hw.env"
start_mock 1.08 git
mkdir -p "$w/vita-git/ux0/data/testgame"
rc=0; (unset VITA_IP VPK; HW_GAME=$w/ggame/tools/vita/hw.game "$tools/hw_check.sh") > "$w/check-git.txt" 2>&1 || rc=$?
check "hw_check reads tools/vita/hw.env" "grep -q 'hw_check: ready' '$w/check-git.txt'"
rc=0; (unset VITA_IP VPK; HW_GAME=$w/ggame/tools/vita/hw.game "$tools/hw_run.sh" "$w/out-git" 30 frames=1200) > "$w/run-git.txt" 2>&1 || rc=$?
check "hw_run reads hw.env and finds HW_VPK from the game's root" "[ $rc = 0 ] && grep -q 'frames= reached' '$w/out-git/hw-run.txt'"
check "the run's account names the commit" "grep -q \"app: eboot.bin (.*, test.vpk, $(git -C "$w/ggame" rev-parse --short HEAD), eboot SHA-1 \" '$w/out-git/hw-run.txt'"
check "hw-run.env: HEAD= the checkout's commit" "grep -qx \"HEAD=$(git -C "$w/ggame" rev-parse HEAD)\" '$w/out-git/hw-run.env'"
kill $mock; wait $mock 2>/dev/null || true

[ $fails = 0 ] && echo "hw_run_test: ok" || { echo "hw_run_test: $fails failed (logs in $w)"; trap - EXIT; exit 1; }
