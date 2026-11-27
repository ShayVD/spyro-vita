#!/usr/bin/env bash
# v3k_run.sh KEYS= against a fake Vita3K window (v3k_keys_fake.swift) that logs the key events it receives: a key
# script's presses reach the run's own process, mapped by the run's Vita3K config.yml; a bad script is refused
# before the lock. macOS with swiftc, and the app this runs from allowed to post events (System Settings, Privacy &
# Security, Accessibility); the fake's window comes to the front for a moment. Elsewhere it says it is skipped.
#   targets/vita/tools/test/v3k_keys_test.sh
set -euo pipefail
[ "$(uname -s)" = Darwin ] && command -v swiftc > /dev/null || { echo "v3k_keys_test: skipped (macOS and swiftc only)"; exit 0; }
tools=$(cd "$(dirname "$0")/.." && pwd)
w=$(cd "$(mktemp -d "${TMPDIR:-/tmp}/v3k_keys_test.XXXXXX")" && pwd)
trap 'rc=$?; kill $(jobs -p) 2> /dev/null || true; rm -rf "$w"; exit $rc' EXIT
fails=0
check() { if eval "$2"; then echo "  ok   $1"; else echo "  FAIL $1"; fails=$((fails + 1)); fi; }

mkdir -p "$w/game/build" "$w/fs"
echo vpk > "$w/game/build/mock.vpk"
cat > "$w/game/hw.game" <<'G'
HW_DATA=ux0:data/mock
HW_LOG=mock.log
HW_STOP_RE='frames=[0-9]+ reached'
HW_VPK=build/mock.vpk
G
swiftc -O -o "$w/vita3k" "$tools/test/v3k_keys_fake.swift"
# the run's Vita3K config: cross moved to KeyB, the rest Vita3K's defaults
printf 'log-level: 0\nkeyboard-button-cross: KeyB\n' > "$w/config.yml"
export HW_GAME="$w/game/hw.game" GAME_ROOT="$w/game" V3K_FS="$w/fs" VITA3K="$w/vita3k" V3K_CONFIG="$w/config.yml" \
  V3K_POLL=1 FAKE_LOG="$w/fs/ux0/data/mock/mock.log"
run() { local r=0; "$tools/v3k_run.sh" "$@" > "$w/out.txt" 2>&1 || r=$?; echo $r; }

echo "v3k_keys_test:"
cat > "$w/keys.txt" <<'K'
log ready 20         # the window is up
press cross 0.1      # KeyB (11) by this config
press select+l1      # right Shift (60) then KeyQ (12)
down lstick-up       # KeyW (13)
up lstick-up
press key:Escape     # the fake's stop line
K
check "a key script plays into the run: exit 0, keys=0" '[ "$(KEYS="$w/keys.txt" run "$w/o1" 60)" = 0 ] && grep -q "^keys=0$" "$w/o1/v3k-run.txt"'
L="$w/o1/mock.log"
check "the presses arrive in order, mapped by the run's config" 'grep "^key \|^flags" "$L" | tr "\n" "," | grep -q "^key down 11,key up 11,flags 60 shift=1,key down 12,key up 12,flags 60 shift=0,key down 13,key up 13,key down 53,key up 53,$"'
check "the steps logged" 'grep -q "down cross" "$w/o1/keys.log" && grep -q "v3k_keys: done" "$w/o1/keys.log" && [ -f "$w/o1/keys.txt" ]'
printf 'press bogus\n' > "$w/bad.txt"
mkdir "$w/fs/ux0/data/mock.lock" # held: a refused script must not wait for it
check "a bad script: exit 1 before the lock" '[ "$(V3K_LOCK_WAIT=0 KEYS="$w/bad.txt" run "$w/o2" 60)" = 1 ] && grep -q "bogus: not a Vita input" "$w/out.txt" && ! grep -q "held by" "$w/out.txt"'
rmdir "$w/fs/ux0/data/mock.lock"
printf 'log never 2\n' > "$w/slow.txt"
check "a log step that times out: keys=3, the run goes on to its own end" '[ "$(KEYS="$w/slow.txt" run "$w/o3" 6)" = 3 ] && grep -q "^keys=3$" "$w/o3/v3k-run.txt"'
[ $fails = 0 ] && echo "v3k_keys_test: ok" || { echo "v3k_keys_test: $fails failed"; exit 1; }
