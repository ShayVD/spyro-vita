# The PC build for Linux on ARM, aarch64 (docs/pc-build.md, D250 O5), with
# clang, inside PC_ARCH=arm64 tools/pc/tc (tools/docker/pc-arm64.Dockerfile)
# or on any aarch64 Linux with clang 19 and SDL3: the offset model, linked as
# linuxa64 is but dynamic. Encore's toolchain (third_party/encore/targets/pc/
# cmake/pc-linux-arm64.toolchain.cmake, D279) under spyro's names; spyro
# forces its own src/compat/p32.h (CMakeLists.txt), so not the kit's header:
#   cmake -S . -B build/pc-linux-arm64 -G Ninja -DSPYRO_HOST=pc \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/pc-linux-arm64.toolchain.cmake
set(LP64_P32_H "" CACHE FILEPATH "spyro forces src/compat/p32.h (CMakeLists.txt)")
if(SPYRO_CLANG)
  set(LP64_CLANG "${SPYRO_CLANG}" CACHE STRING "clang for the PC build")
endif()
if(SPYRO_SDL3_PREFIX)
  set(ENC_SDL3_PREFIX "${SPYRO_SDL3_PREFIX}" CACHE PATH "SDL3's install prefix")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../third_party/encore/targets/pc/cmake/pc-linux-arm64.toolchain.cmake")
set(SPYRO_PC_TARGET ${ENC_PC_TARGET})
