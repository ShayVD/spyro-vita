# arm64 macOS with Apple's clang (the Command Line Tools or Xcode), natively on an Apple-silicon Mac: a 4-byte-pointer
# game's headless host in the offset model (lp64/docs/low-image.md, "macOS"). No Docker:
#   cmake -S . -B build/macos -G Ninja -DCMAKE_TOOLCHAIN_FILE=<kit>/lp64/cmake/macos.toolchain.cmake
# lp64_p32.h, forced into every unit, turns LP64_P32_OFFSET on for arm64: every P32F slot is a 32-bit offset from the
# executable's Mach-O header (lp64_p32_base is __mh_execute_header's C name). The executable is a PIE, as every arm64
# macOS program: the kernel slides it above __PAGEZERO's 4 GB, which keeps every host mapping out of the low 4 GB, so
# a pointer cut to 32 bits crashes there as Linux's reservation makes it; the offsets are the image's own, so they
# don't change with the slide. lp64/runtime/p32_image checks the image at start (its end from the Mach-O segments).
# LP64_P32_CHECK=ON is the checking build.
# From spyro-vita cmake/macos.toolchain.cmake @ 36d9e07 (MIT; D265).
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "the oldest macOS")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(LP64_P32_H "${CMAKE_CURRENT_LIST_DIR}/../runtime/p32/lp64_p32.h" CACHE FILEPATH
    "forced into every C and C++ unit (empty: leave it to the game)")
option(LP64_P32_CHECK "the checking build: an encoded host pointer stops with its file and line" OFF)
set(lp64_flags "")
if(LP64_P32_H)
  string(APPEND lp64_flags " -include ${LP64_P32_H}")
endif()
if(LP64_P32_CHECK)
  string(APPEND lp64_flags " -DLP64_P32_CHECK=1")
endif()
set(CMAKE_C_FLAGS_INIT "${lp64_flags}")
set(CMAKE_CXX_FLAGS_INIT "${lp64_flags}")
