#!/usr/bin/env bash
# The spike's FTP round trip to a PS4 running GoldHEN's FTP server (port 2121)
# (docs/ps4-build.md, D180). Every run on a PS4 needs the owner's explicit
# yes first; this script only moves files, the install is done on the PS4.
#   PS4_IP=192.168.x.y tools/ps4/spike/ftp.sh upload [PKG]   → /data/pkg/
#   PS4_IP=192.168.x.y tools/ps4/spike/ftp.sh log [OUTDIR]   ← /data/spyro-spike/spike.log
set -euo pipefail
: "${PS4_IP:?set PS4_IP to the address of the PS4}"
port=${PS4_FTP_PORT:-2121}
root=$(cd "$(dirname "$0")/../../.." && pwd)
case "${1:-}" in
upload)
  pkg=${2:-$root/build/ps4-spike/IV0000-SPYT00001_00-SPYROP2SPIKE0000.pkg}
  curl -fS --ftp-create-dirs -T "$pkg" "ftp://$PS4_IP:$port/data/pkg/"
  echo "uploaded ${pkg##*/} to /data/pkg/; install it with the GoldHEN Package Installer"
  ;;
log)
  dir=${2:-$root/logs/p2/hw}
  mkdir -p "$dir"
  curl -fS -o "$dir/spike.log" "ftp://$PS4_IP:$port/data/spyro-spike/spike.log"
  echo "fetched $dir/spike.log"
  ;;
*)
  echo "usage: $0 upload [PKG] | log [OUTDIR]" >&2
  exit 2
  ;;
esac
