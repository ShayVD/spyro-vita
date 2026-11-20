#!/usr/bin/env bash
# Build the P2 spike fpkg (docs/ps4-build.md, D180): tools/ps4/spike/main.c
# → spike.elf → eboot.bin (create-fself) → a fake PKG (create-gp4, PkgTool).
# Not the game: no game data, no LiveArea art (icon0.png is a plain grey
# square made here). Throwaway title ID SPYT00001.
#   on the Mac:     tools/ps4/sdk.sh && tools/ps4/spike/build.sh [OUT]
#   in Docker:      tools/ps4/tc tools/ps4/spike/build.sh [OUT]
# OUT defaults to build/ps4-spike. The tools come from PS4_CC, PS4_LD,
# PS4_OOBIN and OO_PS4_TOOLCHAIN (set by the image, or by sdk.sh --env).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
if [ -z "${OO_PS4_TOOLCHAIN:-}" ]; then
  eval "$("$root/tools/ps4/sdk.sh" --env)"
fi
OO=$OO_PS4_TOOLCHAIN
out=${1:-$root/build/ps4-spike}
mkdir -p "$out"
out=$(cd "$out" && pwd)

TITLE="Spyro PS4 spike"
TITLE_ID=SPYT00001
CONTENT_ID=IV0000-${TITLE_ID}_00-SPYROP2SPIKE0000
VERSION=1.00
# The Piglet sample's program ID and auth info (OpenOrbis samples/piglet):
# the game will need Piglet (GLES2), so the spike measures under the same
# rights. Read from the SDK, not copied here.
PAID=0x3800000000000035
AUTHINFO=$(sed -n 's/^AUTHINFO *:= *"\([0-9A-Fa-f]*\)".*/\1/p' "$OO/samples/piglet/Makefile")
[ ${#AUTHINFO} -eq 272 ] || { echo "no Piglet AUTHINFO in $OO/samples/piglet/Makefile" >&2; exit 1; }

commit=$(git -C "$root" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
cflags=(--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -fms-extensions -O1 -g0
        -Wall -Wextra -Wno-unused-parameter -Werror
        -isysroot "$OO" -isystem "$OO/include"
        "-ffile-prefix-map=$root=." "-ffile-prefix-map=$OO=/oo"
        "-DSPIKE_COMMIT=\"$commit\"")
"$PS4_CC" "${cflags[@]}" -c "$here/main.c" -o "$out/main.o"
"$PS4_LD" -m elf_x86_64 -pie --script "$OO/link.x" --eh-frame-hdr --build-id=none \
  "--defsym=__bss_start=ADDR(.bss)" "--defsym=_end=ADDR(.bss)+SIZEOF(.bss)" \
  -L"$OO/lib" -lc -lkernel "$OO/lib/crt1.o" "$out/main.o" -o "$out/spike.elf"

stage=$out/stage
rm -rf "$stage" && mkdir -p "$stage/sce_sys/about" "$stage/sce_module"
# relative paths: create-fself writes them into the eboot
cd "$out"
"$PS4_OOBIN/create-fself" -in=spike.elf -out=spike.oelf \
  --eboot stage/eboot.bin --paid "$PAID" --authinfo "$AUTHINFO" > create-fself.log
cd "$stage"
cp "$OO/samples/piglet/sce_sys/about/right.sprx" sce_sys/about/
cp "$OO/samples/piglet/sce_module/libc.prx" "$OO/samples/piglet/sce_module/libSceFios2.prx" sce_module/
python3 "$here/icon.py" sce_sys/icon0.png

P=$PS4_OOBIN/PkgTool.Core
sfo=sce_sys/param.sfo
{
  "$P" sfo_new $sfo
  "$P" sfo_setentry $sfo APP_TYPE --type Integer --maxsize 4 --value 1
  "$P" sfo_setentry $sfo APP_VER --type Utf8 --maxsize 8 --value "$VERSION"
  "$P" sfo_setentry $sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
  "$P" sfo_setentry $sfo CATEGORY --type Utf8 --maxsize 4 --value gd
  "$P" sfo_setentry $sfo CONTENT_ID --type Utf8 --maxsize 48 --value "$CONTENT_ID"
  "$P" sfo_setentry $sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
  "$P" sfo_setentry $sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
  "$P" sfo_setentry $sfo TITLE --type Utf8 --maxsize 128 --value "$TITLE"
  "$P" sfo_setentry $sfo TITLE_ID --type Utf8 --maxsize 12 --value "$TITLE_ID"
  "$P" sfo_setentry $sfo VERSION --type Utf8 --maxsize 8 --value "$VERSION"
} > "$out/sfo.log"

files="eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png sce_module/libc.prx sce_module/libSceFios2.prx"
"$PS4_OOBIN/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "$files" > "$out/create-gp4.log"
# create-gp4 stamps the build time; the commit's time keeps the pkg reproducible
ts=$(TZ=UTC git -C "$root" log -1 --format=%cd --date=format-local:'%Y-%m-%d %H:%M:%S' 2>/dev/null || echo '2026-11-20 00:00:00')
sed "s|<volume_ts>.*</volume_ts>|<volume_ts>$ts</volume_ts>|" pkg.gp4 > pkg.gp4.tmp && mv pkg.gp4.tmp pkg.gp4
rm -f "$out/$CONTENT_ID.pkg"
"$P" pkg_build pkg.gp4 "$out" > "$out/pkg_build.log"

cd "$out"
if command -v sha256sum >/dev/null; then sum=(sha256sum); else sum=(shasum -a 256); fi
"${sum[@]}" spike.elf stage/eboot.bin stage/sce_sys/param.sfo "$CONTENT_ID.pkg" | tee SHA256SUMS
echo "built $out/$CONTENT_ID.pkg"
