#!/usr/bin/env bash
# The game's fpkg from its linked ELF (docs/ps4-build.md, D193): the P2
# spike's steps (tools/ps4/spike/build.sh, D180) for the game.
#   tools/ps4/pkg.sh ELF OUTDIR
# ELF → eboot.bin (create-fself, the Piglet sample's PAID and auth info) → a
# fake PKG (create-gp4, PkgTool).
# - A test build: title ID SPYT00002, "Spyro PS4 (test)", a plain grey
#   icon0.png.
# - The release (PS4_RELEASE=1, D246): title ID SPYP00001, "Spyro PS4",
#   APP_VER PS4_APP_VER (cmake/version.cmake), icon0.png and pic1.png from
#   PS4_ART (tools/ps4/art.py: the Vita's LiveArea images, the owner's
#   decision, D246), and the licence texts in PS4_LICENSES (a folder) as
#   licenses/ in the package: the eboot links OpenOrbis's GPL-3.0 crt1.o, so
#   the fpkg is offered under GPL-3.0 as a whole (D246).
# The PS4's sce_module PRXs come from OpenOrbis's Piglet sample, and
# param.sfo's CATEGORY gde and FORMAT obs from its Makefile (2Ship2Harkinian-
# PS4 sets the same; with gd, Piglet's eglGetDisplay refused us, D245).
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

if [ "${PS4_RELEASE:-0}" = 1 ]; then
  : "${PS4_APP_VER:?PS4_APP_VER (NN.NN) for the release}" "${PS4_ART:?PS4_ART for the release}"
  : "${PS4_LICENSES:?PS4_LICENSES for the release}"
  [[ "$PS4_APP_VER" =~ ^[0-9][0-9]\.[0-9][0-9]$ ]] || { echo "PS4_APP_VER $PS4_APP_VER is not NN.NN" >&2; exit 1; }
  TITLE="Spyro PS4"
  TITLE_ID=SPYP00001
  CONTENT_ID=IV0000-${TITLE_ID}_00-SPYROPS4RELEASE0
  VERSION=$PS4_APP_VER
else
  TITLE="Spyro PS4 (test)"
  TITLE_ID=SPYT00002
  CONTENT_ID=IV0000-${TITLE_ID}_00-SPYROPS4GAME0000
  VERSION=1.00
fi
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
files="eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png sce_module/libc.prx sce_module/libSceFios2.prx"
if [ "${PS4_RELEASE:-0}" = 1 ]; then
  cp "$PS4_ART/icon0.png" "$PS4_ART/pic1.png" sce_sys/
  files="$files sce_sys/pic1.png"
  mkdir -p licenses
  for f in "$PS4_LICENSES"/*; do
    cp "$f" licenses/
    files="$files licenses/$(basename "$f")"
  done
else
  python3 "$root/tools/ps4/spike/icon.py" sce_sys/icon0.png
fi

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

"$PS4_OOBIN/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" --files "$files" > "$out/create-gp4.log"
# create-gp4 stamps the build time; the commit's time keeps the pkg reproducible
ts=$(TZ=UTC git -C "$root" log -1 --format=%cd --date=format-local:'%Y-%m-%d %H:%M:%S' 2>/dev/null || echo '2026-11-21 00:00:00')
sed "s|<volume_ts>.*</volume_ts>|<volume_ts>$ts</volume_ts>|" pkg.gp4 > pkg.gp4.tmp && mv pkg.gp4.tmp pkg.gp4
# create-gp4's <rootdir> is a fixed list (sce_sys, sce_module, assets/...):
# the release's licenses/ folder is added to it, or PkgTool can't place it
if [ "${PS4_RELEASE:-0}" = 1 ]; then
  sed 's|</rootdir>|	<dir targ_name="licenses" />\
	</rootdir>|' pkg.gp4 > pkg.gp4.tmp && mv pkg.gp4.tmp pkg.gp4
  grep -q '<dir targ_name="licenses" />' pkg.gp4 || { echo "pkg.sh: licenses/ not added to pkg.gp4" >&2; exit 1; }
fi
rm -f "$out/$CONTENT_ID.pkg"
"$P" pkg_build pkg.gp4 "$out" > "$out/pkg_build.log"

cd "$out"
if command -v sha256sum >/dev/null; then sum=(sha256sum); else sum=(shasum -a 256); fi
"${sum[@]}" spyro.elf.pkg-in stage/eboot.bin stage/sce_sys/param.sfo "$CONTENT_ID.pkg" > SHA256SUMS
echo "built $out/$CONTENT_ID.pkg"
