#!/usr/bin/env bash
# Struct layouts: decomp C for mipsel + Sony's PsyQ headers (the matching
# build's tree and image) vs the linux32 port's objects (i686 + PsyCross).
# Run after tools/build_matching.sh and a linux32 build:
#   tools/layoutcheck.sh            -> build/layout/{mips,host}.txt + report
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build/layout"
mkdir -p "$out"
docker run --rm --platform linux/arm64 -v "$root/build:/b" s1_dev_env_arm64 bash -c '
  set -e; cd /b/s1-matching; mkdir -p /b/layout/mips; : > /b/layout/mips.txt
  for f in src/*.c src/gamestates/*.c src/overlays/titlescreen.c; do
    o=/b/layout/mips/$(echo "$f" | tr / _).o
    mipsel-linux-gnu-gcc -c -g -O0 -fno-eliminate-unused-debug-types -w \
      -DSPLAT -DMODERN_COMPILER -DOVERLAY_NAME=titlescreen_code -Iinclude -Ipsyq/include \
      -ffreestanding -fno-builtin -funsigned-char -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=return-mismatch -o "$o" "$f"
    mipsel-linux-gnu-readelf --debug-dump=info "$o" >> /b/layout/mips.txt
  done'
"$root/tools/tc" bash -c '
  set -e; : > build/layout/host.txt
  for o in $(find build/linux32/CMakeFiles/spyro_game_main.dir build/linux32/CMakeFiles/spyro_game_ovl.dir/gen/ovl/titlescreen_code -name "*.o"); do
    i686-linux-gnu-readelf --debug-dump=info "$o" >> build/layout/host.txt
  done'
python3 "$root/tools/layoutcheck.py" "$out/mips.txt" "$out/host.txt"
