#!/usr/bin/env bash
# Run a game's Vita build on a real PS Vita over Wi-Fi until it exits (config.txt's frames=) or a timeout,
# and collect its log, its files and any new crash dump (vita/docs/hardware-runs.md). From spyro-vita
# tools/vita/hw_run.sh @ e365217 (its decision D77; kh1-decomp carried a cut-down copy), the game's details
# moved into a game file.
#   HW_GAME=tools/vita/hw.game hw_run.sh OUT_DIR [TIMEOUT_S] [config line | @FILE]...
#   HW_GAME=tools/vita/hw.game hw_run.sh --restore      only put back what a killed run left
#   HW_GAME=tools/vita/hw.game hw_run.sh --put-eboot FILE [OUT_DIR]   only install FILE's eboot.bin
# A game calls it through its own tools/vita/hw_run.sh, which sets HW_GAME (hw.game.example lists the
# settings and hooks). @FILE adds the config lines in FILE (# comments skipped). PAD=FILE plays a pad script
# (adds pad=); SAVE=DIR starts HW_SAVE_DIR from a copy of DIR; VPK=FILE runs another build than HW_VPK.
# HW_REBOOT=1 allows a reboot when the app won't stop. HW_KEEP_APP=1 fetches the Vita's eboot.bin before
# the run (OUT_DIR/eboot-before.bin) and puts it back when the run ends, however it ends. HW_ELF=PATH (from
# the game's root) is copied next to each crash dump fetched (psp2core-X.elf), its SHA-1 in hw-run.txt.
# --put-eboot FILE (a VPK or an eboot.bin) stops the app and installs FILE's eboot.bin as a run would, reads
# it back and prints both SHA-1s and READBACK IDENTICAL (exit 0) or READBACK DIFFERS (exit 1); its account
# goes to OUT_DIR/hw-run.txt (default $HW_STATE/put-eboot-TITLEID). It takes the lock like a run.
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
# "quit TITLEID" ("kill TITLEID" on 1.05; only this app, never "quit all" or "destroy"). The eboot.bin goes
# up as eboot.bin.hw-new, is checked for size, and replaces eboot.bin (deleted, then a rename); the next run or
# --restore finishes an eboot.bin.hw-new left without its eboot.bin.
# Each run's account: OUT_DIR/hw-run.txt (the "app:" line names the eboot.bin's and the VPK's SHA-1s and the
# VPK's path) and OUT_DIR/hw-run.env (VPK= VPK_SHA1= EBOOT_SHA1= HEAD=).
# One run at a time on any Vita, across games and checkouts: a lock in $HW_STATE (default
# ~/.cache/vita-port-kit) holding the run's PID makes a second run wait (up to HW_LOCK_WAIT seconds, default
# 1800; then "hw_run: NOT RUN: ..." in hw-run.txt, exit 1). hw_ftp.py refuses to work for anyone else while
# the lock is held; the runner's own calls pass (HW_LOCK_HELD). Run the script in the foreground. Exit status
# 0 only when the run ended at frames= (and, with HW_KEEP_APP=1, the eboot.bin went back).
set -euo pipefail
kit=$(cd "$(dirname "$0")" && pwd)
: "${HW_GAME:?set HW_GAME to the hw.game file of the game (vita/tools/hw.game.example)}"
HW_GAME=$(cd "$(dirname "$HW_GAME")" && pwd)/$(basename "$HW_GAME")
groot=$(git -C "$(dirname "$HW_GAME")" rev-parse --show-toplevel 2>/dev/null || (cd "$(dirname "$HW_GAME")/../.." && pwd)) # the game file is ROOT/tools/vita/hw.game
HW_NAMES=() HW_COLLECT=() HW_HANG_RE= HW_SAVE_DIR= HW_QUIET_S=0 HW_POLL_S=3 HW_TIMEOUT=600 HW_PAD_NAME=pad.txt
env_elf=${HW_ELF:-} env_keep=${HW_KEEP_APP:-} # given in the environment: wins over the game file
HW_ELF= HW_KEEP_APP=0
# shellcheck source=/dev/null
. "$HW_GAME"
HW_ELF=${env_elf:-$HW_ELF} HW_KEEP_APP=${env_keep:-$HW_KEEP_APP}
: "${HW_TITLE_ID:?HW_TITLE_ID missing in $HW_GAME}" "${HW_DATA:?}" "${HW_LOG:?}" "${HW_STOP_RE:?}" "${HW_VPK:?}"
export HW_GAME HW_TITLE_ID HW_DATA
# shellcheck source=/dev/null
[ -z "${VITA_IP:-}" ] && [ -f "$groot/tools/vita/hw.env" ] && . "$groot/tools/vita/hw.env" # VITA_IP given wins
: "${VITA_IP:?set VITA_IP or write it in tools/vita/hw.env}"
export VITA_IP VITA_FTP_PORT VITA_CMD_PORT
state=${HW_STATE:-$HOME/.cache/vita-port-kit}
restore_only=0
[ "${1:-}" = --restore ] && { restore_only=1; set -- "$state/restore-$HW_TITLE_ID"; }
put_file=
if [ "${1:-}" = --put-eboot ]; then
  [ -f "${2:-}" ] || { echo "usage: hw_run.sh --put-eboot FILE.vpk|eboot.bin [OUT_DIR] (no file ${2:-})" >&2; exit 1; }
  put_file=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
  set -- "${3:-$state/put-eboot-$HW_TITLE_ID}"
fi
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
sha1() { shasum < "$1" | cut -c1-40; }
bytes() { wc -c < "$1" | tr -d ' '; }

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

# An eboot.bin upload cut off between its delete and its rename: eboot.bin.hw-new (complete: its size was
# checked before eboot.bin was deleted) into place. One beside an eboot.bin is a cut-off upload: deleted.
recover_eboot() {
  local l
  l=$(v ls "$app" 2>/dev/null) || return 0
  grep -q '^f [0-9]* eboot\.bin\.hw-new$' <<< "$l" || return 0
  if grep -q '^f [0-9]* eboot\.bin$' <<< "$l"; then
    v rm "$app/eboot.bin.hw-new"
    say "deleted $app/eboot.bin.hw-new, an upload that was cut off (eboot.bin is still the one before it)"
  else
    v mv "$app/eboot.bin.hw-new" "$app/eboot.bin"
    say "finished an eboot.bin upload that was cut off: $app/eboot.bin.hw-new renamed to eboot.bin"
  fi
}

# LOCAL into $app/eboot.bin without ever leaving the app without a whole eboot.bin for long: up as
# eboot.bin.hw-new, its size checked, eboot.bin deleted, the new one renamed (ftpvita won't rename onto a
# name that is there), its size checked again.
put_eboot() {
  local n got
  n=$(bytes "$1")
  v put "$1" "$app/eboot.bin.hw-new" || { say "eboot.bin: the upload failed"; return 1; }
  got=$(v size "$app/eboot.bin.hw-new" 2>/dev/null || true)
  [ "$got" = "$n" ] || { say "eboot.bin: $app/eboot.bin.hw-new has ${got:-no} bytes, not $n: eboot.bin left as it was"; return 1; }
  if v size "$app/eboot.bin" > /dev/null 2>&1; then
    v rm "$app/eboot.bin" || { say "eboot.bin: could not delete the old one"; return 1; }
  fi
  v mv "$app/eboot.bin.hw-new" "$app/eboot.bin" || { say "eboot.bin: the rename failed: $app/eboot.bin.hw-new is the new one (--restore renames it)"; return 1; }
  got=$(v size "$app/eboot.bin" 2>/dev/null || true)
  [ "$got" = "$n" ] || { say "eboot.bin: $app/eboot.bin has ${got:-no} bytes after the rename, not $n"; return 1; }
}

# put_eboot, then read it back: both SHA-1s, then READBACK IDENTICAL or READBACK DIFFERS (non-zero).
put_checked() { # LOCAL WHAT
  local a b
  a=$(sha1 "$1")
  say "eboot.bin <- $2: $(bytes "$1") bytes, SHA-1 $a"
  put_eboot "$1" || { say "eboot.bin NOT PUT: $2"; return 1; }
  rm -f "$tmp/readback.bin"
  v get "$app/eboot.bin" "$tmp/readback.bin" || { say "READBACK FAILED: could not fetch $app/eboot.bin"; return 1; }
  b=$(sha1 "$tmp/readback.bin")
  say "eboot.bin read back from $app: $(bytes "$tmp/readback.bin") bytes, SHA-1 $b"
  if [ "$a" = "$b" ]; then
    say "READBACK IDENTICAL"
  else
    say "READBACK DIFFERS: local SHA-1 $a, the Vita's $b"; return 1
  fi
}

pid_lock() {
  local waited=0 owner game
  until mkdir "$lock" 2>/dev/null; do
    owner=$(cat "$lock/pid" 2>/dev/null || true)
    if [ -n "$owner" ] && ! kill -0 "$owner" 2>/dev/null; then
      rm -rf "$lock"; continue # left by a run that was SIGKILLed
    fi
    game=$(cat "$lock/game" 2>/dev/null || echo ?)
    [ $waited = 0 ] && say "waiting for the run with PID $owner ($game)"
    [ $waited -ge "${HW_LOCK_WAIT:-1800}" ] && { say "NOT RUN: $lock held by PID $owner ($game) after ${waited}s (HW_LOCK_WAIT)"; exit 1; }
    sleep 5; waited=$((waited + 5))
  done
  echo $$ > "$lock/pid"
  echo "$HW_TITLE_ID" > "$lock/game"
  export HW_LOCK_HELD=$$ # hw_ftp.py lets the lock's holder through
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
  local n d elf
  for n in ${HW_COLLECT[@]+"${HW_COLLECT[@]}"}; do
    case $(kind "$n") in
      d) rm -rf "${out:?}/$n"; v getdir "$data/$n" "$out/$n" || say "$n: not all fetched" ;;
      f) v get "$data/$n" "$out/$n" ;;
    esac
  done
  for d in $(new_dumps); do
    v get "ux0:data/$d" "$out/$d" && say "crash dump $d (left in ux0:data)" || continue
    [ -n "$HW_ELF" ] || continue
    case $HW_ELF in /*) elf=$HW_ELF ;; *) elf=$groot/$HW_ELF ;; esac
    if [ -f "$elf" ]; then
      cp "$elf" "$out/${d%.psp2dmp}.elf"
      say "its ELF: ${d%.psp2dmp}.elf, a copy of $elf (SHA-1 $(sha1 "$elf"))"
    else
      say "its ELF: no $elf (HW_ELF)"
    fi
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
kept=0 # HW_KEEP_APP: OUT_DIR/eboot-before.bin is the Vita's eboot.bin from before the run
uploaded=0
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
  if [ "$kept" = 1 ] && [ "$uploaded" = 1 ]; then
    if put_checked "$out/eboot-before.bin" "eboot-before.bin, the Vita's from before the run (HW_KEEP_APP)"; then
      say "the eboot.bin from before the run is back"
    else
      say "NOT PUT BACK: the eboot.bin from before the run is $out/eboot-before.bin; tools/vita/hw_run.sh --put-eboot $out/eboot-before.bin puts it back"
      [ "$rc" = 0 ] && rc=1
    fi
  fi
  rm -rf "$tmp"
  [ "$(cat "$lock/pid" 2>/dev/null)" = "$$" ] && rm -rf "$lock"
  exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

: > "$out/hw-run.txt"
if [ -n "$put_file" ]; then # FILE's eboot.bin, checked before waiting for the Vita
  if [ "$(head -c 2 "$put_file")" = PK ]; then
    unzip -p "$put_file" eboot.bin > "$tmp/put.bin" 2>/dev/null || true
  else
    cp "$put_file" "$tmp/put.bin"
  fi
  [ -s "$tmp/put.bin" ] || { say "no eboot.bin in $put_file"; exit 1; }
fi
pid_lock
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
recover_eboot
[ $restore_only = 1 ] && exit 0
if [ -n "$put_file" ]; then
  put_checked "$tmp/put.bin" "$put_file"
  exit # put_checked's status
fi

# The app: its eboot.bin every run; installed whole (promote) when it isn't there or sce_sys/ changed, or
# without promote, copied whole over FTP.
[ -f "$vpk" ] || { say "no $vpk"; exit 1; }
vpk=$(cd "$(dirname "$vpk")" && pwd)/$(basename "$vpk")
mkdir -p "$tmp/vpk"
(cd "$tmp/vpk" && unzip -q "$vpk")
sys=$(cd "$tmp/vpk" && find sce_sys -type f | LC_ALL=C sort | xargs shasum | shasum | cut -c1-40)
sysfile=$state/sce_sys-$HW_TITLE_ID-$VITA_IP
stage=$data/hw-promote
installed=1; v size "$app/eboot.bin" > /dev/null || installed=0
if [ "$HW_KEEP_APP" = 1 ]; then
  if [ $installed = 1 ]; then
    v get "$app/eboot.bin" "$out/eboot-before.bin"
    kept=1
    say "HW_KEEP_APP: the Vita's eboot.bin before the run is eboot-before.bin ($(bytes "$out/eboot-before.bin") bytes, SHA-1 $(sha1 "$out/eboot-before.bin")); it goes back when the run ends"
  else
    say "HW_KEEP_APP: no $app/eboot.bin before the run: nothing to put back"
  fi
fi
eboot=$tmp/vpk/eboot.bin
uploaded=1
if [ $promote = 0 ] && [ $installed = 0 ]; then
  say "$HW_TITLE_ID isn't installed, and this vitacompanion has no promote: install $vpk once with VitaShell"; exit 1
elif [ $promote = 0 ] && [ "$(cat "$sysfile" 2>/dev/null)" != "$sys" ]; then
  say "sce_sys/ changed since the last run to $VITA_IP (or there was none): uploading the whole app over FTP"
  mv "$eboot" "$tmp/eboot.bin"; eboot=$tmp/eboot.bin # the rest first, then the eboot.bin safely
  v putdir "$tmp/vpk" "$app"
  put_eboot "$eboot" || exit 1
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
  until [ "$(v size "$app/eboot.bin" 2>/dev/null)" = "$(bytes "$eboot")" ]; do
    [ $t -ge 60 ] && { say "promoted, but $app/eboot.bin isn't the VPK's after 60 s"; exit 1; }
    sleep 3; t=$((t + 3))
  done
  echo "$sys" > "$sysfile"
else
  put_eboot "$eboot" || exit 1
fi
head=$(git -C "$groot" rev-parse HEAD 2>/dev/null || true)
esha=$(sha1 "$eboot") vsha=$(sha1 "$vpk")
say "app: eboot.bin ($(bytes "$eboot") bytes, $(basename "$vpk"), $(git -C "$groot" rev-parse --short HEAD 2>/dev/null || echo '?'), eboot SHA-1 $esha, VPK $vpk, VPK SHA-1 $vsha)"
printf 'VPK=%q\nVPK_SHA1=%s\nEBOOT_SHA1=%s\nHEAD=%s\n' "$vpk" "$vsha" "$esha" "$head" > "$out/hw-run.env"

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
