#!/usr/bin/env bash
# The game's fpkg from its linked ELF (docs/ps4-build.md, D193): the P2
# spike's steps (tools/ps4/spike/build.sh, D180) for the game.
#   tools/ps4/pkg.sh ELF OUTDIR
# ELF → eboot.bin (create-fself, the Piglet sample's PAID and auth info) → a
# fake PKG (create-gp4, PkgTool). Throwaway title ID SPYT00002, a plain grey
# icon0.png (no LiveArea or Vita art: its licence covers the Vita release
# only). The PS4's sce_module PRXs come from OpenOrbis's Piglet sample, and
# param.sfo's CATEGORY gde and FORMAT obs from its Makefile (2Ship2Harkinian-
# PS4 sets the same; with gd, Piglet's eglGetDisplay refused us, D194).
# The tools come from PS4_OOBIN and OO_PS4_TOOLCHAIN (sdk.sh --env, or the
# Docker image).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
elf=$1
out=$2
: "${OO_PS4_TOOLCHAIN:?eval \"\$(tools/ps4/sdk.sh --env)\" first}"
OO=$OO_PS4_TOOLCHAIN
mkdir -p "$out"
out=$(cd "$out" && pwd)

TITLE="Spyro PS4 (test)"
TITLE_ID=SPYT00002
CONTENT_ID=IV0000-${TITLE_ID}_00-SPYROPS4GAME0000
VERSION=1.00
PAID=0x3800000000000035
AUTHINFO=$(sed -n 's/^AUTHINFO *:= *"\([0-9A-Fa-f]*\)".*/\1/p' "$OO/samples/piglet/Makefile")
[ ${#AUTHINFO} -eq 272 ] || { echo "no Piglet AUTHINFO in $OO/samples/piglet/Makefile" >&2; exit 1; }

python3 "$root/tools/ps4/oelf_check.py" "$elf"

stage=$out/stage
rm -rf "$stage" && mkdir -p "$stage/sce_sys/about" "$stage/sce_module"
cp "$elf" "$out/spyro.elf.pkg-in"
# relative paths: create-fself writes them into the eboot
cd "$out"
"$PS4_OOBIN/create-fself" -in=spyro.elf.pkg-in -out=spyro.oelf \
  --eboot stage/eboot.bin --paid "$PAID" --authinfo "$AUTHINFO" > create-fself.log
cd "$stage"
cp "$OO/samples/piglet/sce_sys/about/right.sprx" sce_sys/about/
cp "$OO/samples/piglet/sce_module/libc.prx" "$OO/samples/piglet/sce_module/libSceFios2.prx" sce_module/
python3 "$root/tools/ps4/spike/icon.py" sce_sys/icon0.png

P=$PS4_OOBIN/PkgTool.Core
sfo=sce_sys/param.sfo
{
  "$P" sfo_new $sfo
  "$P" sfo_setentry $sfo APP_TYPE --type Integer --maxsize 4 --value 1
  "$P" sfo_setentry $sfo APP_VER --type Utf8 --maxsize 8 --value "$VERSION"
  "$P" sfo_setentry $sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
  "$P" sfo_setentry $sfo CATEGORY --type Utf8 --maxsize 4 --value gde
  "$P" sfo_setentry $sfo FORMAT --type Utf8 --maxsize 4 --value obs
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
ts=$(TZ=UTC git -C "$root" log -1 --format=%cd --date=format-local:'%Y-%m-%d %H:%M:%S' 2>/dev/null || echo '2026-11-21 00:00:00')
sed "s|<volume_ts>.*</volume_ts>|<volume_ts>$ts</volume_ts>|" pkg.gp4 > pkg.gp4.tmp && mv pkg.gp4.tmp pkg.gp4
rm -f "$out/$CONTENT_ID.pkg"
"$P" pkg_build pkg.gp4 "$out" > "$out/pkg_build.log"

cd "$out"
if command -v sha256sum >/dev/null; then sum=(sha256sum); else sum=(shasum -a 256); fi
"${sum[@]}" spyro.elf.pkg-in stage/eboot.bin stage/sce_sys/param.sfo "$CONTENT_ID.pkg" > SHA256SUMS
echo "built $out/$CONTENT_ID.pkg"
