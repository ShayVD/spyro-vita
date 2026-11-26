# The PC build for macOS on Apple silicon (docs/pc-build.md, D250 O4), with
# Apple's clang (the Command Line Tools or Xcode), native: the offset model
# (src/compat/p32.h), SDL3 static (tools/pc/sdl3_macos.sh), OpenGL 4.1 core.
#   tools/pc/sdl3_macos.sh
#   cmake -S . -B build/pc-macos -G Ninja -DSPYRO_HOST=pc \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/pc-macos.toolchain.cmake
#   cmake --build build/pc-macos     -> build/pc-macos/Spyro.app
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "the oldest macOS")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(SPYRO_PC_TARGET arm64-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET})
# SDL3, a static library (tools/pc/sdl3_macos.sh builds it there)
set(SPYRO_SDL3_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../build/sdl3-macos" CACHE PATH "SDL3's install prefix")
list(APPEND CMAKE_PREFIX_PATH "${SPYRO_SDL3_PREFIX}")
