#!/usr/bin/env bash
# Install a game's VPK in Vita3K, run it with a config.txt made from the arguments until the game writes its stop
# line, Vita3K ends, or a timeout, and collect the log, shots and save folder:
#   HW_GAME=tools/vita/hw.game targets/vita/tools/v3k_run.sh OUT_DIR [TIMEOUT_S] [config line]...
# The game's settings are the hw.game the hardware tools read (hw.game.example): HW_DATA, HW_LOG, HW_STOP_RE,
# HW_HANG_RE, HW_FAIL_RE, HW_VPK, HW_PAD_NAME, HW_SAVE_DIR, HW_TIMEOUT. An optional hook in it, v3k_config_base, prints the
# run's first config.txt lines (default: realtime=0, unless REALTIME=1). The game repo's root is GAME_ROOT, else
# the git top level of HW_GAME's folder.
# PAD=FILE plays a pad script (copied to HW_DATA/HW_PAD_NAME, pad= added); SAVE=DIR starts the save folder from a
# copy; VPK=FILE runs another build; CAPTURE=1 grabs the Vita3K window (macOS) at each hold.req the game writes,
# as OUT_DIR/win-V.png, answering hold.ack (the game's shot_hold protocol, spyro-vita docs/vita-testing.md).
# OUT_DIR/capture.log says which grabs failed: "shot V: no capture (R)" (1 no window; 2 a one-colour grab that is
# not the game's frame). A one-colour grab whose colour is the game's own VRAM shot of that vblank (the shots folder's
# PNG whose name's digits are V, every pixel that colour: a real black fade) is a capture: "shot V: single-colour
# capture (R,G,B), as the VRAM shot NAME". V3K_WINDOW names another grabber (the tests' fake; nothing is raised).
# KEYS=FILE (macOS) plays a key script into this run's Vita3K as keyboard events (v3k_keys.swift: wait, log RE,
# press cross, press select+l1, down/up lstick-up), which Vita3K's keyboard map turns into buttons and sticks: the
# real controller path, which PAD= bypasses (use one or the other). The script is checked before the lock; the
# events go only to the PID this script started, once its arguments show -w VPK; Vita3K comes to the front once.
# Its steps go to OUT_DIR/keys.log, its exit to v3k-run.txt (keys=0 played, 1 a bad script or no Accessibility
# permission, 2 Vita3K ended first, 3 a log step timed out, stopped: the run ended first). Elsewhere: exit 1.
# VITA3K (the binary), V3K_FS (Vita3K's fs folder), V3K_CONFIG (its config.yml; a copy at log level VITA3K_LOG,
# default 4 = ERROR, goes to OUT_DIR), V3K_LOCK_WAIT (s, default 1800), V3K_POLL (s, default 2).
# The data folder is shared with the owner's own Vita3K use, and the run leaves it as it found it, apart from the
# installed app: once the lock is taken, what the run replaces there (config.txt, HW_LOG, shots/, hold.req,
# hold.ack, HW_SAVE_DIR, HW_PAD_NAME with PAD=, the HW_COLLECT names) is moved aside to OUT_DIR/.orig/; however
# the script ends while it holds the lock, the run's own copies (collected to OUT_DIR first) are removed and the
# owner's moved back, before the lock goes. Only SIGKILL can leave them in OUT_DIR/.orig/, which a later run into
# the same OUT_DIR refuses to overwrite (move them back by hand). Files the game makes under other names stay.
# The VPK argument installs over the installed app with that title id, as before; nothing is put back for the app.
# HW_COLLECT (files or folders in HW_DATA) are copied to OUT_DIR too, besides the log, shots and save folder.
# One run at a time per game: a lock folder next to the data folder holds the run's PID; a second run waits up to
# V3K_LOCK_WAIT seconds, then exits 1 with "held by PID". A lock whose PID is dead, or that has no PID and is over
# a minute old (a run killed as it took it), is taken over; takeovers are serialised by a second lock (.reap), so
# two waiters never both take it. After the stop line Vita3K gets about 3 s to exit by itself. Vita3K is stopped and
# reaped however the script ends (only SIGKILL can leave it). OUT_DIR/v3k-run.txt: result, seconds, VPK, SHA-1.
# Exit: 0 the stop line; 2 Vita3K ended without it (a crash); 3 the timeout (a hang); 4 HW_HANG_RE seen; 1 setup;
# 7 HW_FAIL_RE seen (optional: a line the game writes when the route's goal was missed; the run stops at once, and
# the line wins over a stop line found in the same look; v3k-run.txt has result=fail_line and fail_line=THE LINE).
# Vita3K is not a Vita: its timing is not evidence (targets/vita/docs/gotchas.md 24).
# From spyro-vita tools/vita/vita3k_run.sh @ bb62f38, the game's paths and names made settings and the result an
# exit code; DISC= and GRAB= stay spyro-vita's.
set -euo pipefail
out=${1:-}
[ -n "$out" ] || { echo "usage: v3k_run.sh OUT_DIR [TIMEOUT_S] [config line]..."; exit 1; }
shift
[ -n "${HW_GAME:-}" ] && [ -f "$HW_GAME" ] || { echo "v3k_run: HW_GAME must name the game's hw.game"; exit 1; }
HW_GAME=$(cd "$(dirname "$HW_GAME")" && pwd)/$(basename "$HW_GAME")
HW_COLLECT=()
HW_HANG_RE=
HW_FAIL_RE=
HW_PAD_NAME=pad.txt
HW_SAVE_DIR= # as hw_run.sh: no save folder unless the game names one
HW_TIMEOUT=600
# shellcheck disable=SC1090
. "$HW_GAME"
for v in HW_DATA HW_LOG HW_STOP_RE HW_VPK; do
  [ -n "${!v:-}" ] || { echo "v3k_run: $v missing in $HW_GAME"; exit 1; }
done
tools=$(cd "$(dirname "$0")" && pwd)
root=${GAME_ROOT:-$(git -C "$(dirname "$HW_GAME")" rev-parse --show-toplevel 2>/dev/null ||
  (cd "$(dirname "$HW_GAME")/../.." && pwd))} # the game file is ROOT/tools/vita/hw.game
timeout=$HW_TIMEOUT
if [[ ${1:-} =~ ^[0-9]+$ ]]; then timeout=$1; shift; fi
fs=${V3K_FS:-$HOME/Library/Application Support/Vita3K/Vita3K/fs}
data="$fs/${HW_DATA/://}"
vita3k=${VITA3K:-/Applications/Vita3K.app/Contents/MacOS/Vita3K}
cfg_src=${V3K_CONFIG:-$HOME/Library/Application Support/Vita3K/Vita3K/config.yml}
vpk=${VPK:-$root/$HW_VPK}
poll=${V3K_POLL:-2}
[[ $poll =~ ^[1-9][0-9]*$ ]] || { echo "v3k_run: V3K_POLL must be a whole number of seconds"; exit 1; }
[ -f "$vpk" ] || { echo "v3k_run: no VPK at $vpk"; exit 1; }
mkdir -p "$data" "$out"
orig="$out/.orig"
[ ! -e "$orig" ] || { echo "v3k_run: $orig holds the owner's files from a killed run: move them back to $data first"; exit 1; }

keys=
if [ -n "${KEYS:-}" ]; then # checked (and the injector built) before the lock: a bad script never waits for it
  [ "$(uname -s)" = Darwin ] || { echo "v3k_run: KEYS= posts macOS keyboard events: macOS only (use PAD= here)"; exit 1; }
  [ -f "$KEYS" ] || { echo "v3k_run: no key script at $KEYS"; exit 1; }
  keys="${TMPDIR:-/tmp}/v3k_keys"
  if [ ! "$keys" -nt "$tools/v3k_keys.swift" ]; then # built to a temporary name: runs share it
    if ! swiftc -O -o "$keys.$$" "$tools/v3k_keys.swift"; then
      rm -f "$keys.$$"; echo "v3k_run: swiftc could not build the key injector; KEYS= needs it"; exit 1
    fi
    mv -f "$keys.$$" "$keys"
  fi
  cp "$KEYS" "$out/keys.txt"
  kmap=(); [ -f "$cfg_src" ] && kmap=("$cfg_src")
  "$keys" --check "$out/keys.txt" ${kmap[@]+"${kmap[@]}"} || { echo "v3k_run: KEYS=$KEYS refused"; exit 1; }
fi

pid=
cap=
keyp=
keys_rc=
lock="$data.lock"
stop_keys() {
  [ -n "$keyp" ] || return 0
  if kill -0 "$keyp" 2>/dev/null; then
    kill "$keyp" 2>/dev/null || true; wait "$keyp" 2>/dev/null || true; keys_rc=stopped
  else
    keys_rc=0; wait "$keyp" 2>/dev/null || keys_rc=$?
  fi
  keyp=
}
stop_vita3k() {
  [ -n "$cap" ] && { kill "$cap" 2>/dev/null || true; wait "$cap" 2>/dev/null || true; }
  cap=
  stop_keys
  if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
    kill -9 "$pid" 2>/dev/null || true
  fi
  [ -n "$pid" ] && { wait "$pid" 2>/dev/null || true; }
  pid=
}
locked=0
kept=()
aside() { # move the owner's NAMEs in the data folder to OUT_DIR/.orig (when there), for put_back
  local name k
  for name; do
    [ -n "$name" ] || continue
    case /$name/ in */../*|*/./*|//*) echo "v3k_run: $name: not a name inside the data folder"; exit 1 ;; esac
    for k in ${kept[@]+"${kept[@]}"}; do [ "$k" = "$name" ] && continue 2; done
    kept+=("$name")
    if [ -e "$data/$name" ] || [ -L "$data/$name" ]; then
      mkdir -p "$orig/$(dirname "$name")"; mv "$data/$name" "$orig/$name"
    fi
  done
}
put_back() { # the run's copies out, the owner's back (or none, when there was none)
  local name ok=1
  for name in ${kept[@]+"${kept[@]}"}; do
    rm -rf "${data:?}/$name" || ok=0
    if [ -e "$orig/$name" ] || [ -L "$orig/$name" ]; then
      mv "$orig/$name" "$data/$name" || { ok=0; echo "v3k_run: could not put back $data/$name (it is in $orig)"; }
    fi
  done
  kept=()
  [ "$ok" = 0 ] || rm -rf "$orig"
}
cleanup() {
  stop_vita3k
  [ "$locked" = 1 ] && put_back
  { [ "$locked" = 1 ] || [ "$(cat "$lock/pid" 2>/dev/null)" = "$$" ]; } && rm -rf "$lock"
  return 0
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

old() { [ -n "$(find "$1" -maxdepth 0 -mmin +1 2>/dev/null)" ]; } # last changed over a minute ago
# Take over the lock left by OWNER (a dead PID, or "" for a lock with no PID): only under the .reap lock, and only
# when the lock still is that one, so a lock another waiter has just taken over is left alone.
reap() {
  mkdir "$lock.reap" 2>/dev/null || { old "$lock.reap" && rmdir "$lock.reap" 2>/dev/null; return 1; }
  local r=1
  if [ "$(cat "$lock/pid" 2>/dev/null || true)" = "$1" ] && { [ -n "$1" ] || old "$lock"; }; then
    rm -rf "$lock.stale.$$"
    mv "$lock" "$lock.stale.$$" 2>/dev/null && rm -rf "$lock.stale.$$" && r=0
  fi
  rmdir "$lock.reap" 2>/dev/null || true
  return $r
}
waited=0
retried=0
until mkdir "$lock" 2>/dev/null; do
  if [ ! -e "$lock" ]; then # gone since the mkdir; twice in a row: it can't be made (permissions, disk)
    [ "$retried" = 0 ] || { echo "v3k_run: cannot create $lock"; exit 1; }
    retried=1; continue
  fi
  retried=0
  # a stray file in the lock's place counts as a lock with no PID
  owner=$(cat "$lock/pid" 2>/dev/null || true)
  if { [ -n "$owner" ] && ! kill -0 "$owner" 2>/dev/null; } || { [ -z "$owner" ] && old "$lock"; }; then
    reap "$owner" && continue # left by a run that was killed
    [ "$waited" -ge "${V3K_LOCK_WAIT:-1800}" ] && { echo "v3k_run: cannot take over $lock"; exit 1; }
    sleep 1; waited=$((waited + 1)); continue
  fi
  [ "$waited" -ge "${V3K_LOCK_WAIT:-1800}" ] && { echo "v3k_run: $lock held by PID ${owner:-none yet}"; exit 1; }
  [ "$waited" = 0 ] && echo "v3k_run: waiting for the run with PID ${owner:-none yet}"
  sleep 5; waited=$((waited + 5))
done
locked=1
echo $$ > "$lock/pid"
# PAD and SAVE are copied first: they may be the owner's own files in the data folder
[ -z "${PAD:-}" ] || cp "$PAD" "$out/pad.txt"
[ -z "${SAVE:-}" ] || { rm -rf "$out/save-start"; cp -R "$SAVE" "$out/save-start"; }
aside config.txt "$HW_LOG" shots hold.req hold.ack "$HW_SAVE_DIR" ${HW_COLLECT[@]+"${HW_COLLECT[@]}"}
[ -z "${PAD:-}" ] || aside "$HW_PAD_NAME"

if declare -F v3k_config_base > /dev/null; then
  v3k_config_base > "$data/config.txt"
else
  : > "$data/config.txt"
  [ "${REALTIME:-0}" = 1 ] || echo "realtime=0" >> "$data/config.txt"
fi
for line in "$@"; do echo "$line" >> "$data/config.txt"; done
rm -rf "$data/shots" "$data/$HW_LOG" "$data/hold.req" "$data/hold.ack"
if [ -n "$HW_SAVE_DIR" ]; then # empty: the game has no save folder (never rm -rf the data folder itself)
  rm -rf "${data:?}/$HW_SAVE_DIR"; mkdir -p "$data/$HW_SAVE_DIR"
  [ -z "${SAVE:-}" ] || cp -R "$out/save-start/." "$data/$HW_SAVE_DIR/"
fi
if [ -n "${PAD:-}" ]; then
  cp "$out/pad.txt" "$data/$HW_PAD_NAME"
  echo "pad=$HW_DATA/$HW_PAD_NAME" >> "$data/config.txt"
fi
cfgarg=()
if [ -f "$cfg_src" ]; then
  sed "s/^log-level: .*/log-level: ${VITA3K_LOG:-4}/" "$cfg_src" > "$out/vita3k-config.yml"
  cfgarg=(-c "$out/vita3k-config.yml")
fi
winid=
if [ "${CAPTURE:-0}" = 1 ]; then
  winid=${V3K_WINDOW:-"${TMPDIR:-/tmp}/v3k_window"}
  if [ -z "${V3K_WINDOW:-}" ] && [ ! "$winid" -nt "$tools/v3k_window.swift" ]; then # built to a temporary name: runs share it
    if ! swiftc -O -o "$winid.$$" "$tools/v3k_window.swift"; then
      rm -f "$winid.$$"; echo "v3k_run: warning: swiftc could not build the window grabber; CAPTURE=1 needs it"; exit 1
    fi
    mv -f "$winid.$$" "$winid"
  fi
  echo "shot_hold=20000" >> "$data/config.txt"
fi

# A .vpk argument installs (over an older install) and runs it.
"$vita3k" --frontend sdl ${cfgarg[@]+"${cfgarg[@]}"} -w "$vpk" > "$out/vita3k.txt" 2>&1 &
pid=$!
# one_colour V "one-colour R G B A": succeeds (saying so) when the game's VRAM shot of vblank V is all R,G,B (each
# channel within 4, the grabber's own limit) and the grab was opaque; else fails saying why
one_colour() {
  local v=$1 word r g b a more f n shot= rgb
  read -r word r g b a more <<< "$2"
  [ "$word" = one-colour ] && [ -n "$a" ] && [ -z "$more" ] || { echo "the grabber gave no colour"; return 1; }
  rgb="$r,$g,$b"
  [ "$a" -ge 250 ] 2>/dev/null || { echo "a transparent grab ($rgb alpha $a)"; return 1; }
  for f in "$data/shots/"*.png; do
    [ -f "$f" ] || continue
    n=$(basename "$f" .png | tr -dc 0-9)
    [ -n "$n" ] && [ "$((10#$n))" = "$((10#$v))" ] && { shot=$f; break; }
  done
  [ -n "$shot" ] || { echo "one colour ($rgb) and no VRAM shot of $v to compare"; return 1; }
  if python3 "$tools/shots_cmp.py" --one-colour "$rgb" "$shot" --tol 4 > /dev/null 2>&1; then
    echo "$rgb, as the VRAM shot $(basename "$shot")"
  else
    echo "one colour ($rgb), but the VRAM shot $(basename "$shot") is not"; return 1
  fi
}
if [ -n "$winid" ]; then
  ( while :; do
      if [ -f "$data/hold.req" ]; then
        v=$(tr -dc 0-9 < "$data/hold.req" 2>/dev/null || true)
        [ -n "$v" ] || { sleep 0.05; continue; } # still being written
        rm -f "$data/hold.req"
        # no grab, or a blank one: the window wasn't drawn (covered by another); bring this Vita3K forward and
        # try once more
        r=0; c=$("$winid" "$out/win-$v.png" "$pid" 2>> "$out/capture.log") || r=$?
        if [ $r != 0 ]; then
          [ -n "${V3K_WINDOW:-}" ] || osascript -e \
            "tell application \"System Events\" to set frontmost of (first process whose unix id is $pid) to true" \
            > /dev/null 2>> "$out/capture.log" || true
          sleep 1; r=0; c=$("$winid" "$out/win-$v.png" "$pid" 2>> "$out/capture.log") || r=$?
        fi
        if [ $r = 2 ]; then # one colour: the game's frame when its own VRAM shot of V is that colour too
          why=$(one_colour "$v" "$c") && r=0
          if [ $r = 0 ]; then echo "shot $v: single-colour capture ($why)" >> "$out/capture.log"
          else echo "shot $v: no capture (2): $why" >> "$out/capture.log"; fi
        elif [ $r != 0 ]; then
          echo "shot $v: no capture ($r)" >> "$out/capture.log"
        fi
        : > "$data/hold.ack"
      fi
      sleep 0.05
    done ) &
  cap=$!
fi
log="$data/$HW_LOG"
if [ -n "$keys" ]; then
  # only this run's Vita3K: the PID started above, once its arguments show the VPK (another session's Vita3K runs
  # another VPK, or the same one under another PID)
  kpid=
  for _ in $(seq 1 50); do
    kill -0 "$pid" 2>/dev/null || break
    case " $(ps -o command= -p "$pid" 2>/dev/null) " in *" -w $vpk "*) kpid=$pid; break ;; esac
    sleep 0.1
  done
  if [ -n "$kpid" ]; then
    kmap=(); [ -f "$out/vita3k-config.yml" ] && kmap=("$out/vita3k-config.yml")
    "$keys" "$kpid" "$out/keys.txt" "$log" ${kmap[@]+"${kmap[@]}"} > "$out/keys.log" 2>&1 &
    keyp=$!
  else
    echo "v3k_run: warning: Vita3K (PID $pid) never showed -w $vpk: no keys sent" | tee "$out/keys.log"
    keys_rc=1
  fi
fi
result=timeout
t=0
failed() { [ -n "$HW_FAIL_RE" ] && grep -Eq -e "$HW_FAIL_RE" "$log" 2>/dev/null; }
while :; do
  if failed; then result=fail_line; break; fi
  if grep -Eq -e "$HW_STOP_RE" "$log" 2>/dev/null; then result=stop; break; fi
  if [ -n "$HW_HANG_RE" ] && grep -Eq -e "$HW_HANG_RE" "$log" 2>/dev/null; then result=hang_line; break; fi
  if ! kill -0 "$pid" 2>/dev/null; then
    if failed; then result=fail_line
    elif grep -Eq -e "$HW_STOP_RE" "$log" 2>/dev/null; then result=stop; else result=exited; fi
    break
  fi
  [ "$t" -ge "$timeout" ] && break
  sleep "$poll"; t=$((t + poll))
done
if [ "$result" = stop ]; then # let the game finish writing and exit by itself
  for _ in 1 2 3 4 5 6; do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
fi
stop_vita3k
cp "$data/config.txt" "$out/config.txt"
mkdir -p "$(dirname "$out/$HW_LOG")" # HW_LOG may be in a folder of HW_DATA (log/game.log)
cp "$log" "$out/$HW_LOG" 2>/dev/null || echo "v3k_run: no $HW_LOG"
[ -d "$data/shots" ] && { cp "$data/shots/"*.png "$out/" 2>/dev/null || true; }
[ -z "$HW_SAVE_DIR" ] || { rm -rf "${out:?}/$HW_SAVE_DIR"; cp -R "$data/$HW_SAVE_DIR" "$out/$HW_SAVE_DIR"; }
for name in ${HW_COLLECT[@]+"${HW_COLLECT[@]}"}; do
  [ -n "$name" ] && [ "$name" != "$HW_SAVE_DIR" ] && [ -e "$data/$name" ] || continue
  rm -rf "${out:?}/$name"; mkdir -p "$out/$(dirname "$name")"; cp -R "$data/$name" "$out/$name"
done
sha=$( (sha1sum "$vpk" 2>/dev/null || shasum "$vpk") | cut -d' ' -f1)
printf 'result=%s\nseconds=%s\nvpk=%s\nvpk_sha1=%s\n' "$result" "$t" "$vpk" "$sha" > "$out/v3k-run.txt"
if [ "$result" = fail_line ]; then
  fline=$(grep -E -m 1 -e "$HW_FAIL_RE" "$out/$HW_LOG" 2>/dev/null || true)
  echo "fail_line=$fline" >> "$out/v3k-run.txt"
  echo "v3k_run: the fail line (HW_FAIL_RE): $fline"
fi
if [ -n "$keys" ]; then
  echo "keys=$keys_rc" >> "$out/v3k-run.txt"
  [ "$keys_rc" = 0 ] || echo "v3k_run: warning: the key script did not play to its end (keys=$keys_rc, $out/keys.log)"
fi
echo "v3k_run: $result after ${t}s, $(ls "$out" | grep -c '\.png$' || true) pictures in $out"
case $result in
  stop) exit 0 ;;
  exited) exit 2 ;;
  timeout) exit 3 ;;
  hang_line) exit 4 ;;
  fail_line) exit 7 ;;
esac
