#!/usr/bin/env bash
# v3k_run.sh against a fake Vita3K (a script that plays the game): the stop line, a crash, a hang, a hang line,
# the lock, the config lines, the collected files, no Vita3K left behind. Needs bash and python3 only.
#   vita/tools/test/v3k_run_test.sh
set -euo pipefail
tools=$(cd "$(dirname "$0")/.." && pwd)
w=$(cd "$(mktemp -d "${TMPDIR:-/tmp}/v3k_run_test.XXXXXX")" && pwd) # no // (a TMPDIR ending in /)
trap 'rc=$?; kill $(jobs -p) 2> /dev/null || true; rm -rf "$w"; exit $rc' EXIT
fails=0
check() { if eval "$2"; then echo "  ok   $1"; else echo "  FAIL $1"; fails=$((fails + 1)); fi; }

mkdir -p "$w/game/build" "$w/fs"
echo vpk > "$w/game/build/mock.vpk"
cat > "$w/game/hw.game" <<'EOF'
HW_TITLE_ID=MOCK00001
HW_DATA=ux0:data/mock
HW_LOG=mock.log
HW_STOP_RE='frames=[0-9]+ reached'
HW_HANG_RE='watchdog: hang'
HW_VPK=build/mock.vpk
HW_TIMEOUT=600
HW_PAD_NAME=pad.txt
HW_SAVE_DIR=save
HW_COLLECT=(save logs/extra.txt)
EOF
data="$w/fs/ux0/data/mock"
cat > "$w/vita3k" <<'EOF'
#!/usr/bin/env bash
# fake Vita3K: records its arguments, then plays the game config.txt's mock= line asks for
echo "$@" > "$FAKE_DATA/../fake-args.txt"
echo $$ > "$FAKE_DATA/../fake.pid"
mode=$(sed -n 's/^mock=//p' "$FAKE_DATA/config.txt")
echo boot >> "$FAKE_DATA/mock.log"
case $mode in
  stop) mkdir -p "$FAKE_DATA/shots"; printf png > "$FAKE_DATA/shots/10.png"
        [ -d "$FAKE_DATA/save" ] && echo had > "$FAKE_DATA/../fake-save.txt"
        echo save > "$FAKE_DATA/save/card0"; mkdir -p "$FAKE_DATA/logs"; echo extra > "$FAKE_DATA/logs/extra.txt"
        echo "frames=10 reached, exiting" >> "$FAKE_DATA/mock.log"; exit 0 ;;
  crash) exit 139 ;;
  hang) exec sleep 1000 ;;
  hangre) echo "watchdog: hang at 99" >> "$FAKE_DATA/mock.log"; exec sleep 1000 ;;
esac
EOF
chmod +x "$w/vita3k"
export HW_GAME="$w/game/hw.game" GAME_ROOT="$w/game" V3K_FS="$w/fs" VITA3K="$w/vita3k" FAKE_DATA="$data" V3K_POLL=1 \
  V3K_CONFIG="$w/no-config.yml" # never the user's own Vita3K config
mkdir -p "$data"

run() { local r=0; "$tools/v3k_run.sh" "$@" > "$w/out.txt" 2>&1 || r=$?; echo $r; }
gone() { [ -s "$w/fs/ux0/data/fake.pid" ] && ! kill -0 "$(cat "$w/fs/ux0/data/fake.pid")" 2>/dev/null; }

echo "v3k_run_test:"
echo "pad line" > "$w/pad.txt"
check "stop line: exit 0" '[ "$(PAD="$w/pad.txt" run "$w/o1" 30 mock=stop frames=10)" = 0 ]'
check "stop line: result recorded" 'grep -q "^result=stop" "$w/o1/v3k-run.txt"'
check "stop line: log, shot, save, config collected" '[ -f "$w/o1/mock.log" ] && [ -f "$w/o1/10.png" ] && [ -f "$w/o1/save/card0" ] && grep -q "^frames=10$" "$w/o1/config.txt"'
check "stop line: HW_COLLECT's nested file collected" '[ -f "$w/o1/logs/extra.txt" ]'
check "realtime=0 by default" 'grep -q "^realtime=0$" "$w/o1/config.txt"'
check "the pad: copied and named in config.txt" 'grep -q "^pad=ux0:data/mock/pad.txt$" "$w/o1/config.txt" && [ -f "$w/o1/pad.txt" ]'
check "nothing of the owner before: nothing of the run left after" '[ ! -e "$data/config.txt" ] && [ ! -e "$data/pad.txt" ] && [ ! -e "$data/mock.log" ] && [ ! -e "$data/shots" ] && [ ! -e "$data/save" ] && [ ! -e "$data/logs/extra.txt" ] && [ ! -e "$w/o1/.orig" ]'
check "installs the game's VPK" 'grep -q -- "-w $w/game/build/mock.vpk" "$w/fs/ux0/data/fake-args.txt"'
check "the VPK's SHA-1 recorded" 'grep -q "^vpk_sha1=[0-9a-f]\{40\}$" "$w/o1/v3k-run.txt"'
printf 'owner=1\nkeep me\n' > "$w/owner-config.txt"; cp "$w/owner-config.txt" "$data/config.txt"
# the owner's log, save folder and shots in the data folder, and a reference copy of them
mkdir -p "$w/ref/save" "$w/ref/shots"
echo "owner log" > "$w/ref/mock.log"; echo "owner card" > "$w/ref/save/card0"; printf ownerpng > "$w/ref/shots/1.png"
owner_files() { cp -R "$w/ref/mock.log" "$w/ref/save" "$w/ref/shots" "$data/"; }
owner_back() { cmp -s "$data/mock.log" "$w/ref/mock.log" && diff -r "$data/save" "$w/ref/save" > /dev/null &&
  diff -r "$data/shots" "$w/ref/shots" > /dev/null; }
no_owner_files() { rm -rf "$data/mock.log" "$data/save" "$data/shots"; }
owner_files
echo "owner pad" > "$w/owner-pad.txt"; cp "$w/owner-pad.txt" "$data/pad.txt"
check "the owner's config.txt and pad put back after a stop run" '[ "$(PAD="$w/pad.txt" run "$w/o1b" 30 mock=stop)" = 0 ] && grep -q "^mock=stop$" "$w/o1b/config.txt" && cmp -s "$data/config.txt" "$w/owner-config.txt" && cmp -s "$data/pad.txt" "$w/owner-pad.txt" && [ ! -e "$w/o1b/.orig" ]'
check "the owner's log, save and shots put back after a stop run, the run's collected" 'owner_back && grep -q "reached" "$w/o1b/mock.log" && ! grep -q owner "$w/o1b/mock.log" && grep -q "^save$" "$w/o1b/save/card0" && [ -f "$w/o1b/10.png" ] && [ ! -e "$w/o1b/1.png" ]'
no_owner_files
rm -f "$data/config.txt" "$data/pad.txt"
check "crash: exit 2" '[ "$(run "$w/o2" 30 mock=crash)" = 2 ]'
rm -f "$w/fs/ux0/data/fake.pid"
check "hang: exit 3 after the timeout" '[ "$(run "$w/o3" 3 mock=hang)" = 3 ]'
check "hang: Vita3K stopped" 'gone'
rm -f "$w/fs/ux0/data/fake.pid"
check "hang line: exit 4" '[ "$(run "$w/o4" 30 mock=hangre)" = 4 ]'
check "hang line: Vita3K stopped" 'gone'
check "REALTIME=1: no realtime=0" '[ "$(REALTIME=1 run "$w/o5" 30 mock=stop)" = 0 ] && ! grep -q realtime "$w/o5/config.txt"'
check "the lock is released" '[ ! -d "$data.lock" ]'
"$tools/v3k_run.sh" "$w/o6" 6 mock=hang > /dev/null 2>&1 &
holder=$!
sleep 2
check "a second run waits for the lock, then gives up" '[ "$(V3K_LOCK_WAIT=0 run "$w/o7" 30 mock=stop)" = 1 ] && grep -q "held by PID" "$w/out.txt"'
wait $holder || true
check "no HW_GAME: exit 1" '[ "$(HW_GAME= run "$w/o8" 30)" = 1 ]'
# the lock: left by a dead run, left without a PID (old and fresh), and a runner stopped with SIGTERM
sh -c 'exit 0' & dead=$!; wait $dead || true
mkdir "$data.lock"; echo $dead > "$data.lock/pid"
check "a lock with a dead PID is taken over" '[ "$(V3K_LOCK_WAIT=0 run "$w/l1" 30 mock=stop)" = 0 ] && [ ! -d "$data.lock" ]'
mkdir "$data.lock"; touch -t 202001010000 "$data.lock"
check "an old lock with no PID is taken over" '[ "$(V3K_LOCK_WAIT=0 run "$w/l2" 30 mock=stop)" = 0 ] && [ ! -d "$data.lock" ]'
mkdir "$data.lock"
check "a fresh lock with no PID is waited for" '[ "$(V3K_LOCK_WAIT=0 run "$w/l3" 30 mock=stop)" = 1 ] && grep -q "held by PID none yet" "$w/out.txt" && [ -d "$data.lock" ]'
rmdir "$data.lock"
echo stray > "$data.lock"
check "a fresh file in the lock's place is waited for" '[ "$(V3K_LOCK_WAIT=0 run "$w/l4" 30 mock=stop)" = 1 ] && grep -q "held by PID none yet" "$w/out.txt" && [ -f "$data.lock" ]'
touch -t 202001010000 "$data.lock"
check "an old file in the lock's place is taken over" '[ "$(V3K_LOCK_WAIT=0 run "$w/l5" 30 mock=stop)" = 0 ] && [ ! -e "$data.lock" ]'
chmod a-w "$w/fs/ux0/data" # the lock can't be made (as root it can: the check is then meaningless)
start=$(date +%s)
check "a lock that can't be made: exit 1 at once" '[ "$(V3K_LOCK_WAIT=0 run "$w/l6" 30 mock=stop)" = 1 ] && grep -q "cannot create" "$w/out.txt" && [ $(($(date +%s) - start)) -lt 5 ]'
chmod u+w "$w/fs/ux0/data"
rm -f "$w/fs/ux0/data/fake.pid"
cp "$w/owner-config.txt" "$data/config.txt"; owner_files
"$tools/v3k_run.sh" "$w/t1" 30 mock=hang > /dev/null 2>&1 &
runner=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do [ -s "$w/fs/ux0/data/fake.pid" ] && break; sleep 1; done
kill -TERM $runner
rc=0; wait $runner || rc=$?
check "SIGTERM: exit 143, Vita3K stopped, the lock released" '[ $rc = 143 ] && gone && [ ! -d "$data.lock" ]'
check "SIGTERM: the owner's config.txt, log, save and shots put back" 'cmp -s "$data/config.txt" "$w/owner-config.txt" && owner_back'
no_owner_files
rm -f "$data/config.txt"

# hw.game variants: a missing setting, no save folder, the config_base hook; Vita3K's config.yml copied
grep -v '^HW_LOG=' "$w/game/hw.game" > "$w/game/nolog.game"
mkdir -p "$w/r1/.orig"
check "an OUT_DIR holding a killed run's .orig/ is refused" '[ "$(run "$w/r1" 30 mock=stop)" = 1 ] && grep -q "move them back" "$w/out.txt"'
check "a missing hw.game setting: exit 1" '[ "$(HW_GAME="$w/game/nolog.game" run "$w/g1" 30 mock=stop)" = 1 ] && grep -q "HW_LOG missing" "$w/out.txt"'
grep -v '^HW_SAVE_DIR=' "$w/game/hw.game" > "$w/game/nosave.game"
rm -f "$w/fs/ux0/data/fake-save.txt"
echo disc > "$data/disc.bin"
check "HW_SAVE_DIR unset: no save folder made, the data folder left alone" '[ "$(HW_GAME="$w/game/nosave.game" run "$w/g2" 30 mock=stop)" = 0 ] && [ -f "$data/disc.bin" ] && [ -f "$w/g2/config.txt" ] && [ ! -e "$w/fs/ux0/data/fake-save.txt" ] && [ ! -e "$data/save" ]'
{ cat "$w/game/hw.game"; echo 'v3k_config_base() { echo hooked=1; }'; } > "$w/game/hook.game"
check "the v3k_config_base hook gives the first lines" '[ "$(HW_GAME="$w/game/hook.game" run "$w/g3" 30 mock=stop)" = 0 ] && [ "$(head -1 "$w/g3/config.txt")" = hooked=1 ] && ! grep -q realtime "$w/g3/config.txt"'
printf 'log-level: 0\nother: kept\n' > "$w/v3k.yml"; cp "$w/v3k.yml" "$w/v3k.yml.orig"
check "Vita3K config: a copy at log-level 4, passed with -c, the source untouched" '[ "$(V3K_CONFIG="$w/v3k.yml" run "$w/g4" 30 mock=stop)" = 0 ] && grep -q "^log-level: 4$" "$w/g4/vita3k-config.yml" && grep -q "^other: kept$" "$w/g4/vita3k-config.yml" && grep -q -- "-c $w/g4/vita3k-config.yml" "$w/fs/ux0/data/fake-args.txt" && cmp -s "$w/v3k.yml" "$w/v3k.yml.orig"'
[ $fails = 0 ] && echo "v3k_run_test: ok" || { echo "v3k_run_test: $fails failed"; exit 1; }
