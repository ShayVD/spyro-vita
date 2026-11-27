#!/usr/bin/env bash
# targets/pc/tools/ap_gate against a fake game executable: the stop line, a crash, a hang, a hang line, the lock,
# keys to flags, the pad, a fresh user folder per run, the collected log and shots, the run prefix, ARTIFACT, and
# no game left running. Needs bash only.
#   targets/pc/tools/test/ap_gate_test.sh
set -euo pipefail
tools=$(cd "$(dirname "$0")/.." && pwd)
w=$(cd "$(mktemp -d "${TMPDIR:-/tmp}/ap_gate_test.XXXXXX")" && pwd)
# shellcheck disable=SC2154
trap 'rc=$?; kill $(jobs -p) 2> /dev/null || true; rm -rf "$w"; exit $rc' EXIT
fails=0
check() { if eval "$2"; then echo "  ok   $1"; else echo "  FAIL $1"; fails=$((fails + 1)); fi; }

mkdir -p "$w/game/build" "$w/game/tools/pc"
cat > "$w/game/build/game" <<'EOS'
#!/usr/bin/env bash
# fake game: --mock MODE picks what it does; it logs to $PC_HOME/game.log and writes shots/ under its home
echo "$@" > "$HOME/args.txt"
echo $$ > "$HOME/game.pid"
[ -e "$HOME/old.txt" ] && echo "not fresh" > "$HOME/stale.txt"
echo old > "$HOME/old.txt"
mode=; while [ $# -gt 0 ]; do [ "$1" = --mock ] && mode=$2; shift; done
echo boot >> "$PC_HOME/game.log"
case $mode in
  stop) mkdir -p "$HOME/shots"; printf png > "$HOME/shots/600.png"
        echo "frames=10 reached" >> "$PC_HOME/game.log"; exit 0 ;;
  crash) exit 139 ;;
  hang) exec sleep 1000 ;;
  hangre) echo "watchdog: hang at 99" >> "$PC_HOME/game.log"; exec sleep 1000 ;;
esac
EOS
chmod +x "$w/game/build/game"
cp "$w/game/build/game" "$w/other"
cat > "$w/game/tools/pc/linux.game" <<'EOS'
PC_EXE=build/game
PC_STOP_RE='frames=[0-9]+ reached'
PC_HANG_RE='watchdog: hang'
PC_LOG=game.log
PC_ARGS=(--fullscreen 0)
EOS
cat > "$w/prefix" <<'EOS'
#!/usr/bin/env bash
echo prefixed > "$HOME/prefix.txt"; exec "$@"
EOS
chmod +x "$w/prefix"
export GAME_ROOT="$w/game" PC_POLL=1 AP_GATE_LOCKS="$w/locks"
G=$w/game/tools/pc/linux.game
run() { local r=0; "$tools/ap_gate" run --target pc-linux --game "$G" "$@" > "$w/out.txt" 2>&1 || r=$?; echo $r; }
gone() { [ -s "$1/home/game.pid" ] && ! kill -0 "$(cat "$1/home/game.pid")" 2>/dev/null; }

echo "ap_gate_test (pc):"
check "info: ready, JSON" '"$tools/ap_gate" info --target pc-windows | grep -q "\"status\": \"ready\""'
echo "pad line" > "$w/pad.txt"
check "stop line: exit 0" '[ "$(PAD="$w/pad.txt" run "$w/o1" 30 mock=stop frames=10 shot=600 nosound)" = 0 ]'
check "stop line: run.log, shot, run.txt" '[ -f "$w/o1/run.log" ] && [ -f "$w/o1/600.png" ] && grep -q "^result=stop" "$w/o1/run.txt" && grep -q "^target=pc-linux" "$w/o1/run.txt"'
check "keys become flags, after PC_ARGS" 'grep -q -- "^--fullscreen 0 --mock stop --frames 10 --shot 600 --nosound --pad $w/o1/pad.txt$" "$w/o1/home/args.txt"'
check "config.txt lists the keys" 'grep -q "^frames=10$" "$w/o1/config.txt"'
check "a new OUT per run" '[ "$(run "$w/o1" 30 mock=stop)" = 1 ]'
check "a fresh user folder per run" '[ "$(run "$w/o2" 30 mock=stop)" = 0 ] && [ ! -e "$w/o2/home/stale.txt" ]'
check "crash: exit 2" '[ "$(run "$w/o3" 30 mock=crash)" = 2 ] && grep -q "^result=exited" "$w/o3/run.txt"'
check "hang: exit 3, game stopped" '[ "$(run "$w/o4" 2 mock=hang)" = 3 ] && gone "$w/o4"'
check "hang line: exit 4, game stopped" '[ "$(run "$w/o5" 30 mock=hangre)" = 4 ] && gone "$w/o5"'
mkdir -p "$w/locks/ap_gate-pc-linux.lock"; sleep 1000 & echo $! > "$w/locks/ap_gate-pc-linux.lock/pid"
check "lock busy: exit 5" '[ "$(PC_LOCK_WAIT=1 run "$w/o6" 30 mock=stop)" = 5 ] && grep -q "held by PID" "$w/out.txt"'
kill "$(cat "$w/locks/ap_gate-pc-linux.lock/pid")"; wait 2>/dev/null || true
check "a dead holder's lock is taken over" '[ "$(run "$w/o7" 30 mock=stop)" = 0 ] && [ ! -e "$w/locks/ap_gate-pc-linux.lock" ]'
check "ARTIFACT runs another build" '[ "$(ARTIFACT="$w/other" run "$w/o8" 30 mock=stop)" = 0 ] && grep -q "^artifact=$w/other$" "$w/o8/run.txt"'
echo 'PC_RUN=("$GAME_ROOT/../prefix")' >> "$G"
check "PC_RUN prefixes the executable" '[ "$(run "$w/o9" 30 mock=stop)" = 0 ] && [ -f "$w/o9/home/prefix.txt" ]'
# the lock lives in the main checkout, shared by every worktree's gate
unset AP_GATE_LOCKS
git -C "$w/game" init -q && git -C "$w/game" -c user.name=t -c user.email=t@t commit -q --allow-empty -m init
git -C "$w/game" worktree add -q --detach "$w/wt"
mkdir -p "$w/wt/build" "$w/wt/tools/pc"; cp "$w/game/build/game" "$w/wt/build/"; cp "$G" "$w/wt/tools/pc/"
check "a worktree's run locks in the main checkout" '[ "$(GAME_ROOT="$w/wt" "$tools/ap_gate" run --target pc-linux --game "$w/wt/tools/pc/linux.game" "$w/o11" 4 mock=hang > /dev/null 2>&1 & sleep 2; [ -d "$w/game/runtime/autopilot/ap_gate-pc-linux.lock" ] && echo yes; wait)" = yes ]'
export AP_GATE_LOCKS="$w/locks"
check "setup: no run file" '[ "$("$tools/ap_gate" run --target pc-linux --game "$w/none" "$w/o10" 5 > /dev/null 2>&1; echo $?)" = 1 ]'
check "setup: usage" '[ "$("$tools/ap_gate" bogus > /dev/null 2>&1; echo $?)" = 1 ]'
[ "$fails" = 0 ] && echo "ap_gate_test: all passed" || { echo "ap_gate_test: $fails failed"; exit 1; }
