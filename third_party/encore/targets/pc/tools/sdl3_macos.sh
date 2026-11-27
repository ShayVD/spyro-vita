#!/bin/sh
# SDL3 for a native macOS build (Apple silicon; targets/pc/README.md "macOS"): a static library from the same tag as
# the PC image's (docker/pc.Dockerfile), for arm64 and macOS 13 on, so the app links only system frameworks.
#   targets/pc/tools/sdl3_macos.sh [PREFIX]        default PREFIX: ./build/sdl3-macos (the source in ./build/sdl3-src)
# then the toolchain's ENC_SDL3_PREFIX (targets/pc/cmake/pc-macos.toolchain.cmake) or SDL3_PREFIX for the backend's
# tests (targets/pc/test/run_tests.sh). Needs git, cmake and ninja (Homebrew's) and Apple's Command Line Tools.
# SDL_TAG picks another tag; SDL_SRC another source folder.
# From spyro-vita tools/pc/sdl3_macos.sh @ 36d9e07 (MIT; its D265).
set -e
TAG=${SDL_TAG:-release-3.4.18}
OUT=${1:-$PWD/build/sdl3-macos}
SRC=${SDL_SRC:-$PWD/build/sdl3-src}
[ -d "$SRC/.git" ] || git clone --depth 1 -b "$TAG" https://github.com/libsdl-org/SDL.git "$SRC"
cmake -S "$SRC" -B "$SRC/build-macos" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_INSTALL_PREFIX="$OUT"
cmake --build "$SRC/build-macos"
cmake --install "$SRC/build-macos"
