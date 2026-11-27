# The PC build for Linux on ARM, aarch64 (docs/pc-build.md, D250 O5), with
# clang, inside PC_ARCH=arm64 tools/pc/tc (tools/docker/pc-arm64.Dockerfile)
# or on any aarch64 Linux with clang 19 and SDL3: the offset model
# (src/compat/p32.h), linked as linuxa64 is but dynamic (CMakeLists.txt).
#   cmake -S . -B build/pc-linux-arm64 -G Ninja -DSPYRO_HOST=pc \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/pc-linux-arm64.toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(SPYRO_CLANG clang-19 CACHE STRING "clang for the PC build")
set(SPYRO_PC_TARGET aarch64-linux-gnu)
string(REPLACE "clang" "clang++" spyro_clangxx "${SPYRO_CLANG}")
set(CMAKE_C_COMPILER ${SPYRO_CLANG})
set(CMAKE_CXX_COMPILER ${spyro_clangxx})
set(CMAKE_ASM_COMPILER ${SPYRO_CLANG})
set(CMAKE_C_COMPILER_TARGET ${SPYRO_PC_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${SPYRO_PC_TARGET})
set(CMAKE_ASM_COMPILER_TARGET ${SPYRO_PC_TARGET})
# SDL3, a static library (tools/docker/pc-arm64.Dockerfile builds it there)
set(SPYRO_SDL3_PREFIX /opt/sdl3-linux CACHE PATH "SDL3's install prefix")
list(APPEND CMAKE_PREFIX_PATH "${SPYRO_SDL3_PREFIX}")
