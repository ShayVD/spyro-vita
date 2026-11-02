#!/usr/bin/env bash
# Check that tools/vita/hw_run.sh can reach the Vita: vitacompanion's FTP
# (port 1337) and command port (1338), Spyro Vita installed, the disc files
# and the GPU's shader compiler there. It only reads: nothing on the Vita
# changes (the command port is asked "version" and "nosleep status").
#   tools/vita/hw_check.sh
# VITA_IP, or tools/vita/hw.env (gitignored; VITA_IP=192.168.1.23).
set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
# shellcheck source=/dev/null
[ -z "${VITA_IP:-}" ] && [ -f "$root/tools/vita/hw.env" ] && . "$root/tools/vita/hw.env" # VITA_IP given wins
: "${VITA_IP:?set VITA_IP or write it in tools/vita/hw.env}"
export VITA_IP VITA_FTP_PORT VITA_CMD_PORT
v() { python3 -I "$root/tools/vita/hw_ftp.py" "$@"; }
bad=0
ok() { echo "  ok    $*"; }
no() { echo "  FAIL  $*"; bad=1; }
note() { echo "  note  $*"; }

echo "hw_check: the Vita at $VITA_IP"
if l=$(v ls ux0: 2>&1); then
  ok "FTP on port ${VITA_FTP_PORT:-1337}: ux0: has $(wc -l <<< "$l" | tr -d ' ') entries"
else
  no "FTP on port ${VITA_FTP_PORT:-1337}: $l"
  if ! ping -c 2 -t 4 "$VITA_IP" > /dev/null 2>&1; then
    echo "hw_check: no answer to ping at $VITA_IP: the Vita is asleep or off (press PS to wake it), or this isn't its address"
  elif grep -q 'refused' <<< "$l"; then
    echo "hw_check: $VITA_IP answers ping but nothing listens on ${VITA_FTP_PORT:-1337}: vitacompanion isn't loaded (docs/vita-testing.md), or this address is another device"
  else
    echo "hw_check: $VITA_IP answers ping but not on ${VITA_FTP_PORT:-1337}: is vitacompanion loaded? (docs/vita-testing.md)"
  fi
  exit 1
fi
if r=$(v cmd version 2>&1) && [ "${r#Error}" = "$r" ] && [ "$r" != "(no reply)" ]; then
  ok "command port ${VITA_CMD_PORT:-1338}: vitacompanion $r"
  r=$(v cmd nosleep status 2>&1)
  case $r in
    "No-sleep enabled.") ok "no standby while vitacompanion runs (nosleep on)" ;;
    *) note "\"nosleep status\" -> $r: the Vita can go to standby between runs (send \"nosleep on\" or reboot)" ;;
  esac
else
  no "command port ${VITA_CMD_PORT:-1338}: \"version\" -> $r (hw_run.sh needs vitacompanion 1.08 or later)"
fi
if s=$(v size ux0:app/SPYV00001/eboot.bin 2>/dev/null); then
  ok "Spyro Vita installed (ux0:app/SPYV00001/eboot.bin, $s bytes)"
else
  note "Spyro Vita isn't installed: hw_run.sh installs it (promote)"
fi
if d=$(v ls ux0:data/spyro 2>/dev/null); then
  if grep -q ' manifest.json$' <<< "$d"; then
    ok "disc: the extracted files (ux0:data/spyro/manifest.json)"
  elif grep -qiE ' [^ ]*\.(cue|bin|img)$' <<< "$d"; then
    ok "disc: an image in ux0:data/spyro ($(grep -iE '\.(cue|bin|img)$' <<< "$d" | sed 's/^[df] [0-9]* //' | tr '\n' ' '))"
  else
    no "disc: no manifest.json and no .cue/.bin/.img in ux0:data/spyro (a disc= line in config.txt may point elsewhere)"
  fi
  left=$(grep -E '\.hw-(backup|left-[0-9-]+)$' <<< "$d" | sed 's/^[df] [0-9]* //' | tr '\n' ' ')
  [ -z "$left" ] || note "left by a run that couldn't finish: $left(tools/vita/hw_run.sh --restore puts .hw-backup back)"
else
  no "no ux0:data/spyro folder"
fi
if v size ur0:data/libshacccg.suprx > /dev/null 2>&1 || v size ur0:data/external/libshacccg.suprx > /dev/null 2>&1; then
  ok "ur0:data/libshacccg.suprx (the GPU's shader compiler)"
else
  no "no ur0:data/libshacccg.suprx: the app falls back to the slow CPU renderer"
fi
n=$(v ls ux0:data 2>/dev/null | grep -c ' psp2core-.*\.psp2dmp$')
[ "$n" = 0 ] || note "$n crash dump(s) in ux0:data (psp2core-*.psp2dmp); hw_run.sh only fetches new ones"
[ $bad = 0 ] && echo "hw_check: ready" || echo "hw_check: not ready"
exit $bad
