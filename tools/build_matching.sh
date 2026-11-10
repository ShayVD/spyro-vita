#!/usr/bin/env bash
# Build the decomp's matching PSX.EXE + overlays and check the SHA-1.
#
# Builds from an rsync copy in build/s1-matching so third_party/spyro-1 stays
# pristine; applies patches/spyro-1/*.patch, then patches/spyro-1-region/*.patch
# (the region switches the port shares, D133) to the copy only.
#
# REGION=us (default) is the SHA-1-checked NTSC-U build. REGION=pal or jp
# builds the same source with -DREGION=1 or 2 in build/s1-matching-REGION
# (docs/regions-plan.md §6.1) and, instead of the SHA-1, compares it function
# by function with that release's executable and WAD overlays from
# disc/files-REGION (tools/regions/compare_code.py; the summaries in
# build/s1-matching-REGION/compare-*.txt, the lists in compare-*.json; the
# executable once more with --exact, constants included).
#
# Uses a native arm64 image on Apple Silicon: python and binutils run natively
# and only the i386 gcc2.7.2 cc1 goes through binfmt emulation. Running the whole
# image as linux/amd64 crashed python at random under -j, which left empty
# objects behind (the decomp's Makefile has no pipefail).
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
src="$root/third_party/spyro-1"
region="${REGION:-us}"
case "$region" in
  us) region_n=0; target=all; out="$root/build/s1-matching" ;;
  # the decomp's "all" ends with its own SHA-256 check of the US outputs;
  # "non_matching" builds the same files without it
  pal) region_n=1; target=non_matching; out="$root/build/s1-matching-pal" ;;
  jp) region_n=2; target=non_matching; out="$root/build/s1-matching-jp" ;;
  *) echo "REGION must be us, pal or jp" >&2; exit 1 ;;
esac
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

# Sony's PsyQ headers are user-supplied and gitignored inside the submodule, so
# a fresh worktree has none: fall back to PSYQ_INCLUDE (or the main checkout's).
psyq_inc="${PSYQ_INCLUDE:-$src/psyq/include}"
if [ ! -d "$psyq_inc" ]; then
  main_co="$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)/.."
  psyq_inc="$main_co/third_party/spyro-1/psyq/include"
fi
[ -f "$psyq_inc/libgpu.h" ] || {
  echo "missing PsyQ headers: set PSYQ_INCLUDE to your psyq/include" >&2
  exit 1
}

mkdir -p "$out"
rsync -a --delete --exclude .git --exclude /build --exclude /psyq/include "$src/" "$out/"
rsync -a --delete "$psyq_inc/" "$out/psyq/include/"
for p in "$root"/patches/spyro-1/*.patch "$root"/patches/spyro-1-region/*.patch; do
  [ -e "$p" ] || continue
  (cd "$out" && patch -p1 --forward --quiet < "$p")
done

docker build --platform "$platform" -t "$image" "$out" >/dev/null
docker run --rm --platform "$platform" -v "$out:/s1" "$image" bash -c "
  mkdir -p build/src/overlays build/asm build/wad
  make $target -j$jobs SHELL='/bin/bash -o pipefail' GCC='mipsel-linux-gnu-cpp -DREGION=$region_n'
"

if [ "$region" != us ]; then
  files="$root/disc/files-$region"
  exe=$(python3 -c "import json,sys; print([m['path'] for m in json.load(open(sys.argv[1])) if m['path'][:4] in ('SCES','SCPS')][0])" "$files/manifest.json")
  python3 -I "$root/tools/regions/compare_code.py" exe "$files/$exe" --elf "$out/build/psx.elf" \
    --list --json "$out/compare-exe.json" | tee "$out/compare-exe.txt"
  python3 -I "$root/tools/regions/compare_code.py" wad "$files/WAD.WAD" "$region" --elf "$out/build/psx.elf" \
    --list --json "$out/compare-wad.json" | tee "$out/compare-wad.txt"
  # constants too (--exact): what proves an RV/RT edit (D133)
  python3 -I "$root/tools/regions/compare_code.py" exe "$files/$exe" --elf "$out/build/psx.elf" \
    --exact --list --json "$out/compare-exe-exact.json" | tee "$out/compare-exe-exact.txt"
  exit 0
fi

got="$(shasum -a 1 "$out/build/PSX.EXE" | cut -d' ' -f1)"
if [ "$got" != "$want" ]; then
  echo "PSX.EXE SHA-1 mismatch: $got" >&2
  exit 1
fi
echo "PSX.EXE SHA-1 OK ($got)"
