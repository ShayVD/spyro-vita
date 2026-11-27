# A PC build for Linux on ARM (aarch64), with clang 19 and SDL3, natively on an aarch64 machine or in an arm64 image
# (targets/pc/README.md "Linux on ARM"): the offset model, linked as the headless host is (above 4 GB, not 4 GB-aligned,
# the base the image's start: lp64/cmake/linuxa64.toolchain.cmake, included) but dynamic, since SDL loads the display,
# GL and sound libraries at run time. Link lp64/runtime/low_image/lp64_low_image_dyn.c: it reserves the low 4 GB from
# .preinit_array before any library loads, so a pointer cut to 32 bits points at nothing (LP64_LOW_TRAP=0 skips it).
#   cmake -S . -B build/pc-linux-arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=<kit>/targets/pc/cmake/pc-linux-arm64.toolchain.cmake
# Not D250's "base 0" (link low, decode a bare load): with base 0 a missed P32_ENC works by accident here and breaks on
# macOS; the one add it saves is cheap (spyro-vita D266).
# From spyro-vita cmake/pc-linux-arm64.toolchain.cmake and its pc aarch64 link @ 36d9e07 (MIT; D266).
set(LP64_STATIC OFF CACHE BOOL "dynamic: SDL loads libraries")
include("${CMAKE_CURRENT_LIST_DIR}/../../../lp64/cmake/linuxa64.toolchain.cmake")
set(ENC_PC_TARGET aarch64-linux-gnu)
set(ENC_SDL3_PREFIX /opt/sdl3-linux CACHE PATH "SDL3's install prefix")
list(APPEND CMAKE_PREFIX_PATH "${ENC_SDL3_PREFIX}")
