#!/usr/bin/env bash
# Build the decomp's matching PSX.EXE + overlays and check the SHA-1.
#
# Builds from an rsync copy in build/s1-matching so third_party/spyro-1 stays
# pristine; applies patches/spyro-1/*.patch to the copy only.
#
# Uses a native arm64 image on Apple Silicon: python and binutils run natively
# and only the i386 gcc2.7.2 cc1 goes through binfmt emulation. Running the whole
# image as linux/amd64 crashed python at random under -j, which left empty
# objects behind (the decomp's Makefile has no pipefail).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/third_party/spyro-1"
out="$root/build/s1-matching"
jobs="${JOBS:-6}"
want="84e3728ab94720d0873e2514adf4aade4935e0c5"

case "$(uname -m)" in
  arm64|aarch64) platform=linux/arm64 ;;
  *) platform=linux/amd64 ;;
esac
image="s1_dev_env_${platform#linux/}"

[ -f "$src/tools/maspsx/maspsx.py" ] || {
  echo "missing tools/maspsx: run git submodule update --init --recursive" >&2
  exit 1
}

mkdir -p "$out"
rsync -a --delete --exclude .git --exclude /build "$src/" "$out/"
for p in "$root"/patches/spyro-1/*.patch; do
  [ -e "$p" ] || continue
  (cd "$out" && patch -p1 --forward --quiet < "$p")
done

docker build --platform "$platform" -t "$image" "$out" >/dev/null
docker run --rm --platform "$platform" -v "$out:/s1" "$image" bash -c "
  mkdir -p build/src/overlays build/asm build/wad
  make all -j$jobs SHELL='/bin/bash -o pipefail'
"

got="$(shasum -a 1 "$out/build/PSX.EXE" | cut -d' ' -f1)"
if [ "$got" != "$want" ]; then
  echo "PSX.EXE SHA-1 mismatch: $got" >&2
  exit 1
fi
echo "PSX.EXE SHA-1 OK ($got)"
