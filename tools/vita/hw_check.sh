#!/usr/bin/env bash
# Check that tools/vita/hw_run.sh can reach the Vita: vitacompanion's FTP
# (port 1337) and command port (1338), Spyro Vita installed, the disc files
# and the GPU's shader compiler there. It changes nothing on the Vita except
# turning its screen on (the command port's test).
#   tools/vita/hw_check.sh
# VITA_IP, or tools/vita/hw.env (gitignored; VITA_IP=192.168.1.23).
set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
# shellcheck source=/dev/null
[ -f "$root/tools/vita/hw.env" ] && . "$root/tools/vita/hw.env"
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
  echo "hw_check: is the Vita awake, on the same Wi-Fi, with vitacompanion loaded? (docs/vita-testing.md)"
  exit 1
fi
if r=$(v cmd screen on 2>&1); then
  ok "command port ${VITA_CMD_PORT:-1338}: \"screen on\" -> $r"
else
  no "command port ${VITA_CMD_PORT:-1338}: $r"
fi
if s=$(v size ux0:app/SPYV00001/eboot.bin 2>/dev/null); then
  ok "Spyro Vita installed (ux0:app/SPYV00001/eboot.bin, $s bytes)"
else
  no "Spyro Vita isn't installed: install build/vita/spyro-vita.vpk once with VitaShell"
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
