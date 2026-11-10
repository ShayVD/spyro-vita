#!/usr/bin/env bash
# Run a game's Vita build on a real PS Vita over Wi-Fi until it exits (config.txt's frames=) or a timeout,
# and collect its log, its files and any new crash dump (vita/docs/hardware-runs.md). From spyro-vita
# tools/vita/hw_run.sh @ e365217 (its decision D77; kh1-decomp carried a cut-down copy), the game's details
# moved into a game file.
#   HW_GAME=tools/vita/hw.game hw_run.sh OUT_DIR [TIMEOUT_S] [config line | @FILE]...
#   HW_GAME=tools/vita/hw.game hw_run.sh --restore      only put back what a killed run left
# A game calls it through its own tools/vita/hw_run.sh, which sets HW_GAME (hw.game.example lists the
# settings and hooks). @FILE adds the config lines in FILE (# comments skipped). PAD=FILE plays a pad script
# (adds pad=); SAVE=DIR starts HW_SAVE_DIR from a copy of DIR; VPK=FILE runs another build than HW_VPK.
# HW_REBOOT=1 allows a reboot when the app won't stop.
# The Vita: VITA_IP, or the game's tools/vita/hw.env (gitignored; VITA_IP=192.168.1.23), with vitacompanion
# running (FTP on 1337, commands on 1338): 1.05 (user module only) or 1.06 and later; which commands it has is
# read from its help. Each run uploads the VPK's eboot.bin to ux0:app/TITLEID/. When the VPK's sce_sys/
# (LiveArea, param.sfo) changed since the last run to this Vita, or the app isn't installed, it installs the
# whole app with vitacompanion's promote where there is one (the extracted VPK, staged in HW_DATA/hw-promote);
# without promote (1.05) the whole app goes over FTP, and a first install needs VitaShell.
# The owner's own files in HW_DATA (config.txt, the log, the pad script, HW_NAMES) are renamed to
# NAME.hw-backup on the Vita for the run and renamed back when the script ends, however it ends; only the
# run's own files under those names are deleted. A run killed before it could put them back (SIGKILL, Wi-Fi
# gone) leaves the .hw-backup files: the next run, or --restore, puts them back first, keeping what is in the
# way as NAME.hw-left-TIME. The disc files and anything else are never touched. The app is stopped with
# "quit TITLEID" ("kill TITLEID" on 1.05; only this app, never "quit all" or "destroy").
# One run at a time on any Vita, across games and checkouts: a lock in $HW_STATE (default
# ~/.cache/vita-port-kit) holding the run's PID makes a second run wait (up to HW_LOCK_WAIT seconds, default
# 1800). Run the script in the foreground. Exit status 0 only when the run ended at frames=.
set -euo pipefail
kit=$(cd "$(dirname "$0")" && pwd)
: "${HW_GAME:?set HW_GAME to the hw.game file of the game (vita/tools/hw.game.example)}"
HW_GAME=$(cd "$(dirname "$HW_GAME")" && pwd)/$(basename "$HW_GAME")
groot=$(git -C "$(dirname "$HW_GAME")" rev-parse --show-toplevel 2>/dev/null || (cd "$(dirname "$HW_GAME")/../.." && pwd)) # the game file is ROOT/tools/vita/hw.game
HW_NAMES=() HW_COLLECT=() HW_HANG_RE= HW_SAVE_DIR= HW_QUIET_S=0 HW_POLL_S=3 HW_TIMEOUT=600 HW_PAD_NAME=pad.txt
# shellcheck source=/dev/null
. "$HW_GAME"
: "${HW_TITLE_ID:?HW_TITLE_ID missing in $HW_GAME}" "${HW_DATA:?}" "${HW_LOG:?}" "${HW_STOP_RE:?}" "${HW_VPK:?}"
export HW_GAME HW_TITLE_ID HW_DATA
# shellcheck source=/dev/null
[ -z "${VITA_IP:-}" ] && [ -f "$groot/tools/vita/hw.env" ] && . "$groot/tools/vita/hw.env" # VITA_IP given wins
: "${VITA_IP:?set VITA_IP or write it in tools/vita/hw.env}"
export VITA_IP VITA_FTP_PORT VITA_CMD_PORT
state=${HW_STATE:-$HOME/.cache/vita-port-kit}
restore_only=0
[ "${1:-}" = --restore ] && { restore_only=1; set -- "$state/restore-$HW_TITLE_ID"; }
out=${1:?usage: hw_run.sh OUT_DIR [TIMEOUT_S] [config line | @FILE]...}; shift
timeout=${1:-$HW_TIMEOUT}; [ $# -gt 0 ] && shift
vpk=${VPK:-$groot/$HW_VPK}
data=$HW_DATA
app=ux0:app/$HW_TITLE_ID
lock=$state/hw.lock
poll=$HW_POLL_S
names=(config.txt "$HW_LOG" "$HW_PAD_NAME" ${HW_NAMES[@]+"${HW_NAMES[@]}"})
mkdir -p "$out" "$state"
out=$(cd "$out" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/hw_run.XXXXXX")

v() { python3 -I "$kit/hw_ftp.py" "$@"; }
say() { echo "hw_run: $*" | tee -a "$out/hw-run.txt"; }
stamp() { date +%Y%m%d-%H%M%S; }
fn() { declare -F "$1" > /dev/null; }

# kind of NAME in HW_DATA: d, f, or nothing (listing cached per call)
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
    [ $waited = 0 ] && echo "hw_run: waiting for the run with PID $owner ($(cat "$lock/game" 2>/dev/null || echo ?))"
    sleep 5; waited=$((waited + 5))
  done
  echo $$ > "$lock/pid"
  echo "$HW_TITLE_ID" > "$lock/game"
}

# Is the app still writing its log?
log_grows() {
  local a b
  a=$(v size "$data/$HW_LOG" 2>/dev/null || echo 0)
  sleep "${1:-7}"
  b=$(v size "$data/$HW_LOG" 2>/dev/null || echo 0)
  [ "$b" -gt "$a" ]
}

stopped=0
quit=quit
stop_app() {
  [ "$stopped" = 1 ] && return 0
  stopped=1
  v cmd "$quit" "$HW_TITLE_ID" > /dev/null 2>&1 || true
  sleep 2
  [ "$launched" = 1 ] || return 0
  log_grows && { say "the app still writes its log after $quit; again"; v cmd "$quit" "$HW_TITLE_ID" > /dev/null 2>&1 || true; sleep 3; } || return 0
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

# The run's log, HW_COLLECT and new crash dumps into OUT_DIR.
collected=0
collect() {
  collected=1
  v get "$data/$HW_LOG" "$out/$HW_LOG" 2>/dev/null || say "no $HW_LOG"
  relist
  local n d
  for n in ${HW_COLLECT[@]+"${HW_COLLECT[@]}"}; do
    case $(kind "$n") in
      d) rm -rf "${out:?}/$n"; v getdir "$data/$n" "$out/$n" || say "$n: not all fetched" ;;
      f) v get "$data/$n" "$out/$n" ;;
    esac
  done
  for d in $(new_dumps); do
    v get "ux0:data/$d" "$out/$d" && say "crash dump $d (left in ux0:data)"
  done
  ! fn hw_after_collect || hw_after_collect "$out"
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
help=$(v cmd help) || { say "no reply on $VITA_IP:${VITA_CMD_PORT:-1338}: is the Vita awake, with vitacompanion loaded? (tools/vita/hw_check.sh)"; exit 1; }
has() { awk -v c="$1" '$1 == c { f = 1 } END { exit !f }' <<< "$help"; }
if has quit; then quit="quit"; elif has kill; then quit="kill"; else
  say "vitacompanion's help has neither quit nor kill: it needs 1.05 or later"; say "$help"; exit 1
fi
promote=0; has promote && promote=1
say "vitacompanion: $(has version && v cmd version || echo "1.05-style, no version command"); stops the app with $quit"
v cmd "$quit" "$HW_TITLE_ID" > /dev/null 2>&1 || true # an error reply when it isn't running
sleep 2
recover
[ $restore_only = 1 ] && exit 0

# The app: its eboot.bin every run; installed whole (promote) when it isn't there or sce_sys/ changed, or
# without promote, copied whole over FTP.
[ -f "$vpk" ] || { say "no $vpk"; exit 1; }
mkdir -p "$tmp/vpk"
(cd "$tmp/vpk" && unzip -q "$vpk")
sys=$(cd "$tmp/vpk" && find sce_sys -type f | LC_ALL=C sort | xargs shasum | shasum | cut -c1-40)
sysfile=$state/sce_sys-$HW_TITLE_ID-$VITA_IP
stage=$data/hw-promote
installed=1; v size "$app/eboot.bin" > /dev/null || installed=0
if [ $promote = 0 ] && [ $installed = 0 ]; then
  say "$HW_TITLE_ID isn't installed, and this vitacompanion has no promote: install $vpk once with VitaShell"; exit 1
elif [ $promote = 0 ] && [ "$(cat "$sysfile" 2>/dev/null)" != "$sys" ]; then
  say "sce_sys/ changed since the last run to $VITA_IP (or there was none): uploading the whole app over FTP"
  v putdir "$tmp/vpk" "$app"
  echo "$sys" > "$sysfile"
  say "a changed bubble or LiveArea shows after VitaShell's \"Refresh LiveArea\" (or a reinstall)"
elif [ $installed = 0 ] || [ "$(cat "$sysfile" 2>/dev/null)" != "$sys" ]; then
  say "not installed, or sce_sys/ changed since the last run to $VITA_IP: installing the whole app (promote)"
  relist
  [ -z "$(kind hw-promote)" ] || v rmtree "$stage" # a run's own, left by a failed promote
  v putdir "$tmp/vpk" "$stage"
  reply=$(VITA_CMD_WAIT=180 v cmd promote "$stage")
  say "promote: $reply"
  relist
  [ -z "$(kind hw-promote)" ] || v rmtree "$stage"
  [ "$reply" = Promoted. ] || exit 1
  t=0
  until [ "$(v size "$app/eboot.bin" 2>/dev/null)" = "$(wc -c < "$tmp/vpk/eboot.bin" | tr -d ' ')" ]; do
    [ $t -ge 60 ] && { say "promoted, but $app/eboot.bin isn't the VPK's after 60 s"; exit 1; }
    sleep 3; t=$((t + 3))
  done
  echo "$sys" > "$sysfile"
else
  v put "$tmp/vpk/eboot.bin" "$app/eboot.bin"
fi
say "app: eboot.bin ($(wc -c < "$tmp/vpk/eboot.bin" | tr -d ' ') bytes, $(basename "$vpk"), $(git -C "$groot" rev-parse --short HEAD 2>/dev/null || echo '?'))"

# The owner's files aside: a copy of the small ones here, then renamed.
relist
backup=$state/backup-$HW_TITLE_ID-$VITA_IP
rm -rf "$backup"; mkdir -p "$backup"
[ "$(kind config.txt)" = f ] && v get "$data/config.txt" "$backup/config.txt"
[ -n "$HW_SAVE_DIR" ] && [ "$(kind "$HW_SAVE_DIR")" = d ] && v getdir "$data/$HW_SAVE_DIR" "$backup/$HW_SAVE_DIR" 1048576
for n in "${names[@]}"; do
  if [ -n "$(kind "$n")" ]; then
    v mv "$data/$n" "$data/$n.hw-backup"; moved+=("$n")
  else
    absent+=("$n")
  fi
done
say "set aside: ${moved[*]-nothing} (a copy of config.txt and the small save files in $backup)"

# The run's config.txt (the hook's base lines, then the arguments: later lines win), pad script and save.
cfg=$out/config.txt
: > "$cfg"
owner_cfg=; [ -f "$backup/config.txt" ] && owner_cfg=$backup/config.txt
if fn hw_config_base; then
  hw_config_base "$owner_cfg" >> "$cfg"
elif [ -n "$owner_cfg" ]; then
  grep -vE '^[[:space:]]*pad=' "$owner_cfg" >> "$cfg" || true
  echo >> "$cfg"
fi
for line in "$@"; do
  case $line in
    @*) grep -vE '^[[:space:]]*(#|$)' "${line#@}" >> "$cfg" ;;
    *) echo "$line" >> "$cfg" ;;
  esac
done
if [ -n "${PAD:-}" ]; then
  cp "$PAD" "$out/pad.txt"
  v put "$PAD" "$data/$HW_PAD_NAME"
  echo "pad=$data/$HW_PAD_NAME" >> "$cfg"
fi
v put "$cfg" "$data/config.txt"
if [ -n "$HW_SAVE_DIR" ]; then
  v mkdir "$data/$HW_SAVE_DIR"
  [ -n "${SAVE:-}" ] && v putdir "$SAVE" "$data/$HW_SAVE_DIR"
fi
! fn hw_after_setup || hw_after_setup "$backup"
setup_done=1
dumps_before=$(dumps)

v cmd screen on > /dev/null 2>&1 || true
reply=$(v cmd launch "$HW_TITLE_ID")
launched=1
say "launch: $reply"
t=0 fails=0 how="timeout (${timeout}s)"
# HW_QUIET_S: no FTP at all for a while after the launch (kh1-decomp: polling vitacompanion 1.05's FTP every 10 s
# while the game ran wedged it at frame ~900; only a Vita restart brought it back)
if [ "$HW_QUIET_S" -gt 0 ]; then
  say "waiting ${HW_QUIET_S}s without touching the Vita"
  sleep "$HW_QUIET_S"; t=$HW_QUIET_S
fi
while [ "$t" -lt "$timeout" ]; do
  sleep "$poll"; t=$((t + poll))
  if v get "$data/$HW_LOG" "$out/$HW_LOG" 2>/dev/null; then
    fails=0
    grep -qE "$HW_STOP_RE" "$out/$HW_LOG" && { how="frames= reached"; break; }
    [ -n "$HW_HANG_RE" ] && grep -qE "$HW_HANG_RE" "$out/$HW_LOG" && { how="hang"; break; }
  else
    fails=$((fails + 1))
    [ $((fails * poll)) -ge 30 ] && [ $fails -ge 6 ] && { how="no answer from the Vita for $((fails * poll))s"; break; }
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
echo "hw_run: ${t}s ($how), files in $out"
tail -3 "$out/$HW_LOG" 2>/dev/null || true
[ "$how" = "frames= reached" ] # exit 1 otherwise: a timeout, a hang or a crash
