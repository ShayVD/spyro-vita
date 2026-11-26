# arm64 macOS with Apple's clang (the Command Line Tools or Xcode), native on
# an Apple-silicon Mac: the offset model's host as a Mach-O program (D250 O4).
# src/compat/p32.h turns SPYRO_P32_OFFSET on for arm64, so every P32 slot is
# a 32-bit offset from the executable's Mach-O header. No Docker:
#   cmake -S . -B build/macos -G Ninja -DSPYRO_HOST=macos \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/macos.toolchain.cmake
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "the oldest macOS")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
