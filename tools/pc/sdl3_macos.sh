#!/bin/sh
# SDL3 for the macOS build (docs/pc-build.md, D250 O4): a static library from
# the same tag as the PC image's (tools/docker/pc.Dockerfile), for arm64 and
# macOS 13 on, installed in build/sdl3-macos (gitignored). Once per checkout:
#   tools/pc/sdl3_macos.sh
# then -DSPYRO_SDL3_PREFIX=$PWD/build/sdl3-macos (cmake/pc-macos.toolchain.cmake's default).
set -e
cd "$(dirname "$0")/../.."
TAG=${SDL_TAG:-release-3.4.18}
SRC=build/sdl3-src
OUT=${1:-$PWD/build/sdl3-macos}
[ -d "$SRC/.git" ] || git clone --depth 1 -b "$TAG" https://github.com/libsdl-org/SDL.git "$SRC"
cmake -S "$SRC" -B "$SRC/build-macos" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_INSTALL_PREFIX="$OUT"
cmake --build "$SRC/build-macos"
cmake --install "$SRC/build-macos"
