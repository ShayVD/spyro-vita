#!/usr/bin/env bash
# Run the Vita build on a real PS Vita over Wi-Fi until it exits (config.txt's
# frames=) or a timeout, and collect its log, screenshots and crash dumps: the
# hardware twin of tools/vita/vita3k_run.sh (docs/vita-testing.md, "Hardware
# runs from the Mac"; decisions D77).
#   tools/vita/hw_run.sh OUT_DIR [TIMEOUT_S] [config line | @FILE]...
#   tools/vita/hw_run.sh logs/hw-run1-title 150 @tools/vita/hw-routes/title.cfg
#   tools/vita/hw_run.sh --restore      only put back what a killed run left
# @FILE adds the config lines in FILE (# comments skipped). PAD=FILE plays a
# pad script (linux32 --pad format; adds pad=); SAVE=DIR starts from a copy of
# that memory-card folder (card0, ...); the cards the run leaves, audio.wav
# (wav=1) and any new crash dump (ux0:data/psp2core-*.psp2dmp, D73) are
# copied to OUT_DIR too. REALTIME=1 runs with real-time vblanks instead of
# virtual ones. VPK=FILE runs another build than build/vita/spyro-vita.vpk.
# DEVICE_DEFAULTS=1 leaves out the lines that make a run compare with the host
# (4:3, 30 fps, the host's camera), so the device's own defaults apply: for
# timing runs. HW_REBOOT=1 allows a reboot when the app won't stop.
# The Vita: VITA_IP, or tools/vita/hw.env (gitignored; VITA_IP=192.168.1.23),
# with vitacompanion running (FTP on 1337, commands on 1338) and Spyro Vita
# installed once from its VPK with VitaShell. Each run uploads the VPK's
# eboot.bin to ux0:app/SPYV00001/, and the whole app when its sce_sys/ (the
# LiveArea, param.sfo) changed since the last run to this Vita.
# The owner's own files in ux0:data/spyro (config.txt, save/, shots/,
# spyro.log, audio.wav, pad.txt) are renamed to NAME.hw-backup on the Vita
# for the run and renamed back when the script ends, however it ends; only
# the run's own files under those names are deleted. A run killed before it
# could put them back (SIGKILL, Wi-Fi gone) leaves the .hw-backup files: the
# next run, or --restore, puts them back first, keeping what is in the way
# as NAME.hw-left-TIME. The disc files and anything else are never touched.
# The app is stopped with vitacompanion's destroy (which closes whatever app
# is open), never a reboot unless HW_REBOOT=1 and the app won't stop.
# One run at a time, across worktrees: a lock in $HW_STATE (default
# ~/.cache/spyro-vita) holding the run's PID makes a second run wait (up to
# HW_LOCK_WAIT seconds, default 1800). Run the script in the foreground.
# Exit status 0 only when the run ended at frames=.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
# shellcheck source=/dev/null
[ -f "$root/tools/vita/hw.env" ] && . "$root/tools/vita/hw.env"
: "${VITA_IP:?set VITA_IP or write it in tools/vita/hw.env}"
export VITA_IP VITA_FTP_PORT VITA_CMD_PORT
restore_only=0
[ "${1:-}" = --restore ] && { restore_only=1; set -- "${HW_STATE:-$HOME/.cache/spyro-vita}/restore"; }
out=${1:?usage: hw_run.sh OUT_DIR [TIMEOUT_S] [config line | @FILE]...}; shift
timeout=${1:-120}; [ $# -gt 0 ] && shift
vpk=${VPK:-$root/build/vita/spyro-vita.vpk}
data=ux0:data/spyro
app=ux0:app/SPYV00001
state=${HW_STATE:-$HOME/.cache/spyro-vita}
lock=$state/hw.lock
poll=${HW_POLL_S:-3}
# What the app writes in ux0:data/spyro (src/platform/vita), and the pad
# script: set aside for the run, the run's own deleted after it.
names=(config.txt save shots spyro.log audio.wav pad.txt hold.req hold.ack)
mkdir -p "$out" "$state"
out=$(cd "$out" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hw_run.XXXXXX")

v() { python3 -I "$root/tools/vita/hw_ftp.py" "$@"; }
say() { echo "hw_run: $*" | tee -a "$out/hw-run.txt"; }
stamp() { date +%Y%m%d-%H%M%S; }

# kind of NAME in ux0:data/spyro: d, f, or nothing (listing cached per call)
listing=
relist() { listing=$(v ls "$data"); }
kind() { awk -v n="$1" '{ name = $0; sub(/^[df] [0-9]+ /, "", name); if (name == n) { print $1; exit } }' <<< "$listing"; }
remove() { # a run's own file or folder
  case $(kind "$1") in
    d) v rmtree "$data/$1" ;;
    f) v rm "$data/$1" ;;
  esac
}

# Leftovers of a run that couldn't restore: NAME.hw-backup back to NAME.
recover() {
  relist
  local n left=0
  for n in "${names[@]}"; do
    [ -n "$(kind "$n.hw-backup")" ] || continue
    if [ -n "$(kind "$n")" ]; then
      v mv "$data/$n" "$data/$n.hw-left-$(stamp)"; left=1
    fi
    v mv "$data/$n.hw-backup" "$data/$n"
    say "put back $n from a run that couldn't"
  done
  [ $left = 0 ] || say "what was in the way is kept as $data/NAME.hw-left-*: look, then delete it yourself"
  return 0
}

pid_lock() {
  local waited=0 owner
  until mkdir "$lock" 2>/dev/null; do
    owner=$(cat "$lock/pid" 2>/dev/null || true)
    if [ -n "$owner" ] && ! kill -0 "$owner" 2>/dev/null; then
      rm -rf "$lock"; continue # left by a run that was SIGKILLed
    fi
    [ $waited -ge "${HW_LOCK_WAIT:-1800}" ] && { echo "hw_run: $lock held by PID $owner"; exit 1; }
    [ $waited = 0 ] && echo "hw_run: waiting for the run with PID $owner"
    sleep 5; waited=$((waited + 5))
  done
  echo $$ > "$lock/pid"
}

# Is the app still writing its log? (a stats line every 300 vblanks)
log_grows() {
  local a b
  a=$(v size "$data/spyro.log" 2>/dev/null || echo 0)
  sleep "${1:-7}"
  b=$(v size "$data/spyro.log" 2>/dev/null || echo 0)
  [ "$b" -gt "$a" ]
}

stopped=0
stop_app() {
  [ "$stopped" = 1 ] && return 0
  stopped=1
  v cmd destroy > /dev/null 2>&1 || true
  sleep 2
  [ "$launched" = 1 ] || return 0
  log_grows && { say "the app still writes its log after destroy; again"; v cmd destroy > /dev/null 2>&1 || true; sleep 3; } || return 0
  log_grows || return 0
  if [ "${HW_REBOOT:-0}" = 1 ]; then
    say "the app won't stop: rebooting the Vita (HW_REBOOT=1)"
    v cmd reboot > /dev/null 2>&1 || true
    sleep 30
    local t=0
    until v ls ux0: > /dev/null 2>&1; do
      [ $t -ge 150 ] && { say "the Vita isn't back after the reboot"; return 1; }
      sleep 5; t=$((t + 5))
    done
  else
    say "the app won't stop and HW_REBOOT isn't 1: putting the files back anyway"
  fi
}

# The run's log, shots, cards, audio and new crash dumps into OUT_DIR.
collected=0
collect() {
  collected=1
  v get "$data/spyro.log" "$out/spyro.log" 2>/dev/null || say "no spyro.log"
  relist
  if [ "$(kind shots)" = d ]; then
    rm -rf "$tmp/shots"; v getdir "$data/shots" "$tmp/shots" || say "shots: not all fetched"
    find "$tmp/shots" -name '*.png' -exec mv {} "$out/" \;
  fi
  [ "$(kind audio.wav)" = f ] && v get "$data/audio.wav" "$out/audio.wav"
  rm -rf "$out/save"
  [ "$(kind save)" = d ] && v getdir "$data/save" "$out/save"
  local d
  for d in $(new_dumps); do
    v get "ux0:data/$d" "$out/$d" && say "crash dump $d (left in ux0:data)"
  done
  return 0
}
dumps_before=
dumps() { v ls ux0:data | awk '$1 == "f" && $3 ~ /^psp2core-.*\.psp2dmp$/ { print $3 }'; }
new_dumps() { comm -13 <(sort <<< "$dumps_before") <(dumps | sort) | sed '/^$/d'; }

moved=() # the owner's NAMEs, now NAME.hw-backup
absent=() # NAMEs the owner had none of
launched=0
setup_done=0
cleanup() {
  local rc=$? n ok=1
  trap '' INT TERM HUP
  if [ "$setup_done" = 1 ] || [ ${#moved[@]} -gt 0 ] || [ ${#absent[@]} -gt 0 ]; then
    stop_app || true
    [ "$launched" = 1 ] && [ "$collected" = 0 ] && { collect || true; }
    relist 2>/dev/null || true
    for n in ${moved[@]+"${moved[@]}"} ${absent[@]+"${absent[@]}"}; do
      remove "$n" || ok=0
    done
    for n in ${moved[@]+"${moved[@]}"}; do
      v mv "$data/$n.hw-backup" "$data/$n" || ok=0
    done
    if [ $ok = 1 ]; then
      say "your files in $data are back as they were"
    else
      say "NOT ALL PUT BACK: your files are $data/*.hw-backup on the Vita; tools/vita/hw_run.sh --restore puts them back"
    fi
  fi
  rm -rf "$tmp"
  [ "$(cat "$lock/pid" 2>/dev/null)" = "$$" ] && rm -rf "$lock"
  return $rc
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

pid_lock
: > "$out/hw-run.txt"
v cmd destroy > /dev/null || { say "no reply on $VITA_IP:${VITA_CMD_PORT:-1338}: is vitacompanion running? (tools/vita/hw_check.sh)"; exit 1; }
sleep 2
recover
[ $restore_only = 1 ] && exit 0

# The app: its eboot.bin every run, everything when sce_sys/ changed.
[ -f "$vpk" ] || { say "no $vpk"; exit 1; }
v size "$app/eboot.bin" > /dev/null ||
  { say "Spyro Vita isn't installed on the Vita: install $vpk once with VitaShell"; exit 1; }
mkdir -p "$tmp/vpk"
(cd "$tmp/vpk" && unzip -q "$vpk")
sys=$(cd "$tmp/vpk" && find sce_sys -type f | LC_ALL=C sort | xargs shasum | shasum | cut -c1-40)
if [ "$(cat "$state/sce_sys-$VITA_IP" 2>/dev/null)" != "$sys" ]; then
  say "sce_sys/ changed since the last run to $VITA_IP (or there was none): uploading the whole app"
  v putdir "$tmp/vpk" "$app"
  echo "$sys" > "$state/sce_sys-$VITA_IP"
  say "a changed bubble or LiveArea shows after VitaShell's \"Refresh LiveArea\" (or a reinstall)"
else
  v put "$tmp/vpk/eboot.bin" "$app/eboot.bin"
fi
say "eboot.bin uploaded ($(wc -c < "$tmp/vpk/eboot.bin" | tr -d ' ') bytes, $(basename "$vpk"), $(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo '?'))"

# The owner's files aside: a copy of the small ones here, then renamed.
relist
backup=$state/backup-$VITA_IP
rm -rf "$backup"; mkdir -p "$backup/save"
[ "$(kind config.txt)" = f ] && v get "$data/config.txt" "$backup/config.txt"
if [ "$(kind save)" = d ]; then
  v ls "$data/save" | while read -r t size name; do
    if [ "$t" = f ] && [ "$size" -lt 1048576 ]; then v get "$data/save/$name" "$backup/save/$name"; fi
  done
fi
for n in "${names[@]}"; do
  if [ -n "$(kind "$n")" ]; then
    v mv "$data/$n" "$data/$n.hw-backup"; moved+=("$n")
  else
    absent+=("$n")
  fi
done
say "set aside: ${moved[*]-nothing} (a copy of config.txt and the small save files in $backup)"

# The run's config.txt (the owner's disc= kept), pad script and save.
cfg=$out/config.txt
: > "$cfg"
[ -f "$backup/config.txt" ] && grep -E '^disc=' "$backup/config.txt" >> "$cfg" || true
[ "${REALTIME:-0}" = 1 ] || echo "realtime=0" >> "$cfg"
# As vita3k_run.sh: the host's picture and camera so runs compare with it,
# unless DEVICE_DEFAULTS=1 (later lines win; config_version=3: main.c).
[ "${DEVICE_DEFAULTS:-0}" = 1 ] ||
  printf 'config_version=3\nwidescreen=0\ninterp=0\nfree_camera=0\ncam_invert_x=0\ncam_invert_y=0\n' >> "$cfg"
echo "awake=1" >> "$cfg" # no auto-sleep or screen off while it runs (main.c)
for line in "$@"; do
  case $line in
    @*) grep -vE '^[[:space:]]*(#|$)' "${line#@}" >> "$cfg" ;;
    *) echo "$line" >> "$cfg" ;;
  esac
done
if [ -n "${PAD:-}" ]; then
  cp "$PAD" "$out/pad.txt"
  v put "$PAD" "$data/pad.txt"
  echo "pad=$data/pad.txt" >> "$cfg"
fi
v put "$cfg" "$data/config.txt"
v mkdir "$data/save"
[ -n "${SAVE:-}" ] && v putdir "$SAVE" "$data/save"
# the disc image's one-time check (D60) isn't done again for every run
[ -f "$backup/save/disc-verified.txt" ] && [ ! -f "${SAVE:-/nonexistent}/disc-verified.txt" ] &&
  v put "$backup/save/disc-verified.txt" "$data/save/disc-verified.txt"
setup_done=1
dumps_before=$(dumps)

v cmd screen on > /dev/null || true
reply=$(v cmd launch SPYV00001)
launched=1
say "launch: $reply"
t=0 fails=0 how="timeout (${timeout}s)"
while [ $t -lt "$timeout" ]; do
  sleep "$poll"; t=$((t + poll))
  if v get "$data/spyro.log" "$out/spyro.log" 2>/dev/null; then
    fails=0
    grep -q 'reached, exiting' "$out/spyro.log" && { how="frames= reached"; break; }
    grep -q 'hang: stopping the app for a crash dump' "$out/spyro.log" && { how="hang (D73)"; break; }
  else
    fails=$((fails + 1))
    [ $fails -ge 10 ] && { how="no answer from the Vita for $((fails * poll))s"; break; }
  fi
  [ $((t % (poll * 4))) = 0 ] && [ -n "$(new_dumps 2>/dev/null)" ] && { how="crashed (a new crash dump)"; break; }
done
say "stopped after ${t}s: $how"
# a crash dump takes a while to write: wait until its size settles
case $how in hang*|crashed*)
  w=0 last=
  while [ $w -lt "${HW_DUMP_WAIT:-90}" ]; do
    now=
    [ -n "$(new_dumps 2>/dev/null)" ] && now=$(v ls ux0:data | awk '$3 ~ /^psp2core-/ { print $2, $3 }')
    [ -n "$now" ] && [ "$now" = "$last" ] && break
    last=$now
    sleep 5; w=$((w + 5))
  done ;;
esac
stop_app
collect
echo "hw_run: ${t}s ($how), $(find "$out" -maxdepth 1 -name '*.png' | wc -l | tr -d ' ') shots in $out"
tail -3 "$out/spyro.log" 2>/dev/null || true
[ "$how" = "frames= reached" ] # exit 1 otherwise: a timeout, a hang or a crash
