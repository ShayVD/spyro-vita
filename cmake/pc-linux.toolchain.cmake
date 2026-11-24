# The PC build for x86-64 Linux (docs/pc-build.md, D240), with clang for
# __ptr32 (src/compat/p32.h), inside tools/pc/tc (tools/docker/pc.Dockerfile,
# an amd64 image) or on any x86-64 Linux with clang 19 and SDL3:
#   cmake -S . -B build/pc-linux -G Ninja -DSPYRO_HOST=pc \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/pc-linux.toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(SPYRO_CLANG clang-19 CACHE STRING "clang for the PC build")
set(SPYRO_PC_TARGET x86_64-linux-gnu)
string(REPLACE "clang" "clang++" spyro_clangxx "${SPYRO_CLANG}")
set(CMAKE_C_COMPILER ${SPYRO_CLANG})
set(CMAKE_CXX_COMPILER ${spyro_clangxx})
set(CMAKE_ASM_COMPILER ${SPYRO_CLANG})
set(CMAKE_C_COMPILER_TARGET ${SPYRO_PC_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${SPYRO_PC_TARGET})
set(CMAKE_ASM_COMPILER_TARGET ${SPYRO_PC_TARGET})
# SDL3, a static library (tools/docker/pc.Dockerfile builds it there)
set(SPYRO_SDL3_PREFIX /opt/sdl3-linux CACHE PATH "SDL3's install prefix")
list(APPEND CMAKE_PREFIX_PATH "${SPYRO_SDL3_PREFIX}")
# clang hangs (19) or crashes (16) on a ?: that mixes a __ptr32 and a normal
# pointer (D174): no compile may run for ever.
set(CMAKE_C_COMPILER_LAUNCHER timeout 300)
