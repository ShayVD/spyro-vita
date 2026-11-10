#!/usr/bin/env bash
# Build the VPK in the official VitaSDK image, as GitHub Actions does
# (.github/workflows/build.yml, decisions D57). No disc data and no matching
# build: the game's layout comes from tools/layout/psx.map.
#   docker run --rm -v "$PWD:/work" -w /work vitasdk/vitasdk:latest tools/ci/build_vpk.sh [BUILD_DIR]
# Leaves BUILD_DIR/spyro-vita.vpk (default build/vita). SPYRO_CMAKE_ARGS adds
# CMake options (-e SPYRO_CMAKE_ARGS=-DSPYRO_NATIVE_MEM=OFF: the moved arena
# instead of native memory, decisions D106).
set -euo pipefail
cd "$(dirname "$0")/../.."
out="${1:-build/vita}"
: "${VITASDK:=/usr/local/vitasdk}"
export PATH="$VITASDK/bin:$PATH"

# The code generators read the decomp's YAML.
if ! python3 -c "import yaml" 2>/dev/null; then
  apt-get update -qq && apt-get install -y -qq --no-install-recommends python3-yaml >/dev/null
fi
# The image ships sdl2_vitagl (SDL2 on top of vitaGL); the port is built and
# tested against vdpm's plain sdl2 (GXM renderer for gpu=0, decisions D35).
# The swap asks to remove the conflicting package: answer yes.
if ! vdpm list sdl2 2>/dev/null | grep -q "^sdl2 "; then
  (yes || true) | vdpm install sdl2 >/dev/null
  vdpm list sdl2 | grep -q "^sdl2 " || { echo "vdpm: sdl2 not installed" >&2; exit 1; }
fi
vdpm list vitaGL vitaShaRK libmathneon SceShaccCgExt sdl2 taihen

cmake -S . -B "$out" -G Ninja -DSPYRO_HOST=vita \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DPython3_EXECUTABLE="$(command -v python3)" ${SPYRO_CMAKE_ARGS:-}
cmake --build "$out"

# What went into the VPK, and the Vita's rules for it (vita-port-kit's
# livearea_png.py: eboot.bin, param.sfo, template.xml and the three sce_sys
# PNGs present, not empty, 8-bit indexed at their sizes).
python3 -m zipfile -l "$out/spyro-vita.vpk"
python3 third_party/vita-port-kit/vita/tools/livearea_png.py vpk "$out/spyro-vita.vpk" --livearea
echo "VPK OK: $out/spyro-vita.vpk"
