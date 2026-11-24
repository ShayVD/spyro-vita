#!/usr/bin/env bash
# The PS4 release fpkg, built on the owner's Mac (docs/ps4-plan.md P5, D246).
#
#   tools/ps4/release.sh [--shaders DIR] [--art DIR] [--upload vX.Y.Z]
#
# The shader binaries (disc/ps4-shaders/, gitignored, D245) are never
# committed, so CI can't build this: CI builds the same code without them as a
# check only (.github/workflows/build.yml, job ps4), and never attaches it.
#
# 1. Refuses a tree with uncommitted changes (the pkg must be the commit's).
# 2. Configures build/ps4-release with -DSPYRO_PS4_RELEASE=ON: title ID
#    SPYP00001, APP_VER from cmake/version.cmake, the icon and background from
#    the Vita's LiveArea images, the licence texts in licenses/; the build
#    stops unless every shader has a binary for its current source.
# 3. Builds it twice from clean and compares every SHA-256 (eboot.bin,
#    param.sfo, the pkg): a release must reproduce.
# 4. Archives build/release-ps4/<APP_VER>-<commit>/: spyro-ps4-<APP_VER>.pkg,
#    SHA256SUMS, the unstripped spyro.elf and spyro.map (crash reports; never
#    attached to a release) and manifest.txt. An existing folder is refused.
# 5. --upload vX.Y.Z: HEAD must be that tag, the release must exist and be a
#    DRAFT (CI's tag run makes it); the pkg and its .sha256 are attached. It is
#    never published here: the owner reviews and publishes.
#
# --shaders defaults to disc/ps4-shaders, --art to disc/livearea if it has
# bg.png, else assets/livearea (as the Vita picks its art). Run
# `eval "$(tools/ps4/sdk.sh --env)"` first.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
shaders=$root/disc/ps4-shaders
art=
tag=
while [ $# -gt 0 ]; do
  case $1 in
    --shaders) shaders=$(cd "$2" && pwd); shift 2 ;;
    --art) art=$(cd "$2" && pwd); shift 2 ;;
    --upload) tag=$2; shift 2 ;;
    *) sed -n '2,26p' "$0"; exit 2 ;;
  esac
done
: "${OO_PS4_TOOLCHAIN:?eval \"\$(tools/ps4/sdk.sh --env)\" first}"
if [ -z "$art" ]; then
  if [ -f disc/livearea/bg.png ]; then art=$root/disc/livearea; else art=$root/assets/livearea; fi
fi

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  echo "release.sh: the tree has uncommitted changes; commit them first" >&2
  exit 1
fi
commit=$(git rev-parse HEAD)
short=$(git rev-parse --short=7 HEAD)
describe=$(git describe --tags --always --dirty)
ver=$(sed -n 's/^set(SPYRO_APP_VER "\([0-9.]*\)")$/\1/p' cmake/version.cmake)
[ -n "$ver" ] || { echo "release.sh: no SPYRO_APP_VER in cmake/version.cmake" >&2; exit 1; }
if [ -n "$tag" ]; then
  [ "$(git rev-parse "$tag^{commit}" 2>/dev/null)" = "$commit" ] \
    || { echo "release.sh: HEAD is not $tag" >&2; exit 1; }
fi

arch=$root/build/release-ps4/$ver-$short
[ -e "$arch" ] && { echo "release.sh: $arch exists; releases are archived, never overwritten" >&2; exit 1; }

bdir=$root/build/ps4-release
# a Python with PyYAML (the overlay tools need it): $PYTHON, /usr/bin/python3, python3
python3=
for p in ${PYTHON:-} /usr/bin/python3 "$(command -v python3)"; do
  if [ -x "$p" ] && "$p" -c 'import yaml' 2>/dev/null; then python3=$p; break; fi
done
[ -n "$python3" ] || { echo "release.sh: no python3 with PyYAML (set PYTHON)" >&2; exit 1; }
cmake -S . -B "$bdir" -G Ninja -DSPYRO_HOST=ps4 -DCMAKE_TOOLCHAIN_FILE=cmake/ps4.toolchain.cmake \
  -DPython3_EXECUTABLE="$python3" -DSPYRO_PS4_RELEASE=ON \
  -DSPYRO_PS4_SHADERS="$shaders" -DSPYRO_PS4_ART_SRC="$art" > "$bdir.configure.log" 2>&1 \
  || { tail -20 "$bdir.configure.log"; exit 1; }
pkg=IV0000-SPYP00001_00-SPYROPS4RELEASE0.pkg
for pass in 1 2; do
  cmake --build "$bdir" --target clean > /dev/null
  rm -rf "$bdir/pkg" "$bdir/ps4-art" "$bdir/ps4-licenses" "$bdir/ps4-licenses.stamp" "$bdir/ps4-shader-check.txt"
  echo "release.sh: build $pass of 2"
  cmake --build "$bdir" > "$bdir.build$pass.log" 2>&1 || { tail -30 "$bdir.build$pass.log"; exit 1; }
  grep -q 'shader_check: all .* shaders have a binary' "$bdir.build$pass.log"
  cp "$bdir/pkg/SHA256SUMS" "$bdir.sums$pass"
done
if ! cmp -s "$bdir.sums1" "$bdir.sums2"; then
  echo "release.sh: the two builds differ:" >&2
  diff "$bdir.sums1" "$bdir.sums2" >&2 || true
  exit 1
fi

if command -v sha256sum >/dev/null; then sum=(sha256sum); else sum=(shasum -a 256); fi
mkdir -p "$arch"
out=spyro-ps4-$ver.pkg
cp "$bdir/pkg/$pkg" "$arch/$out"
cp "$bdir/spyro.elf" "$bdir/spyro.map" "$arch/"
(cd "$arch" && "${sum[@]}" "$out" > "$out.sha256" && "${sum[@]}" "$out" spyro.elf > SHA256SUMS)
{
  echo "spyro-ps4 release fpkg (docs/ps4-plan.md P5, D246)"
  echo "file: $out"
  echo "sha256: $("${sum[@]}" "$arch/$out" | cut -d' ' -f1)"
  echo "title_id: SPYP00001"
  echo "content_id: ${pkg%.pkg}"
  echo "app_ver: $ver"
  echo "commit: $commit"
  echo "describe: $describe"
  echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ) on $(uname -sm)"
  echo "reproduced: yes (two clean builds, every SHA-256 the same)"
  echo "toolchain: OpenOrbis $(basename "$OO_PS4_TOOLCHAIN"), $("$PS4_CC" --version | head -1)"
  echo "art: $art"
  echo "shaders: $shaders"
  grep 'shader_check: .*\.bin ok' "$bdir.build2.log" | sed 's/^shader_check: /  /'
  echo "pkg contents (SHA256SUMS of the build):"
  sed 's/^/  /' "$bdir.sums2"
  echo "runtime_validation: Not tested on hardware (edit after the owner's run)"
} > "$arch/manifest.txt"
echo "release.sh: $arch/$out"
cat "$arch/$out.sha256"

if [ -n "$tag" ]; then
  [ "$(gh release view "$tag" --json isDraft --jq .isDraft 2>/dev/null)" = true ] \
    || { echo "release.sh: $tag is not a draft release (or doesn't exist); not uploading" >&2; exit 1; }
  gh release upload "$tag" "$arch/$out" "$arch/$out.sha256" --clobber
  echo "release.sh: attached $out to the draft release $tag (not published)"
fi
