# The PC build for macOS on Apple silicon (docs/pc-build.md, D250 O4), with
# Apple's clang, native: the offset model, SDL3 static, OpenGL 4.1 core.
# Encore's toolchain (third_party/encore/targets/pc/cmake/
# pc-macos.toolchain.cmake, D279) under spyro's names; spyro forces its own
# src/compat/p32.h (CMakeLists.txt), so not the kit's header:
#   third_party/encore/targets/pc/tools/sdl3_macos.sh
#   cmake -S . -B build/pc-macos -G Ninja -DSPYRO_HOST=pc \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/pc-macos.toolchain.cmake
#   cmake --build build/pc-macos     -> build/pc-macos/Spyro.app
set(LP64_P32_H "" CACHE FILEPATH "spyro forces src/compat/p32.h (CMakeLists.txt)")
# SDL3, a static library (sdl3_macos.sh builds it there); SPYRO_SDL3_PREFIX is the old name
if(SPYRO_SDL3_PREFIX)
  set(ENC_SDL3_PREFIX "${SPYRO_SDL3_PREFIX}" CACHE PATH "SDL3's install prefix")
else()
  set(ENC_SDL3_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../build/sdl3-macos" CACHE PATH "SDL3's install prefix")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../third_party/encore/targets/pc/cmake/pc-macos.toolchain.cmake")
set(SPYRO_PC_TARGET ${ENC_PC_TARGET})
