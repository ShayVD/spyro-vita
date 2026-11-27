# aarch64 Linux with clang: a 4-byte-pointer game's headless host in the offset model (lp64/docs/low-image.md, "The
# offset model"), natively on an aarch64 machine or in the arm64 lp64 host image (no qemu):
#   cmake -S . -B build/linuxa64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=<kit>/lp64/cmake/linuxa64.toolchain.cmake
# lp64_p32.h, forced into every unit, turns LP64_P32_OFFSET on for aarch64: every P32F slot is a 32-bit offset from
# lp64_p32_base. The executable is static and non-PIE, linked above 4 GB and not 4 GB-aligned (LP64_IMAGE_BASE,
# 0x2_0123_0000), the base its start:
#  - above 4 GB, as macOS places its image, so a pointer cut to 32 bits points at nothing (lp64_low_image_dyn.c's
#    reservation adds the trap for a dynamic executable);
#  - not 4 GB-aligned: at 0x2_0000_0000 a pointer cut to 32 bits *is* its offset, and a missed P32_ENC works by
#    accident (spyro-vita D262 found both kinds of miss this way, D266 kept it for the desktop build);
#  - -z separate-code: GNU ld for aarch64 otherwise puts the whole image in one RWX segment.
# lp64/runtime/p32_image (lp64_p32_image_start) checks the image at start; LP64_P32_CHECK=ON is the checking build.
# From spyro-vita cmake/linuxa64.toolchain.cmake and CMakeLists.txt's linuxa64 link @ 36d9e07 (MIT; D262, D266). Its
# SPYRO_CLANG is LP64_CLANG, spyro_p32_base is lp64_p32_base.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(LP64_CLANG clang-19 CACHE STRING "clang for the offset model's aarch64 build")
set(LP64_P32_H "${CMAKE_CURRENT_LIST_DIR}/../runtime/p32/lp64_p32.h" CACHE FILEPATH
    "forced into every C and C++ unit (empty: leave it to the game)")
set(LP64_IMAGE_BASE 0x201230000 CACHE STRING "where the image is linked: above 4 GB, not 4 GB-aligned")
option(LP64_STATIC "a static executable (a headless host); OFF for a desktop build that loads libraries" ON)
option(LP64_P32_CHECK "the checking build: an encoded host pointer stops with its file and line" OFF)
string(REPLACE "clang" "clang++" lp64_clangxx "${LP64_CLANG}")
set(CMAKE_C_COMPILER ${LP64_CLANG})
set(CMAKE_CXX_COMPILER ${lp64_clangxx})
set(CMAKE_ASM_COMPILER ${LP64_CLANG})
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_ASM_COMPILER_TARGET aarch64-linux-gnu)
set(lp64_flags "-fno-pic")
if(LP64_P32_H)
  string(APPEND lp64_flags " -include ${LP64_P32_H}")
endif()
if(LP64_P32_CHECK)
  string(APPEND lp64_flags " -DLP64_P32_CHECK=1")
endif()
set(CMAKE_C_FLAGS_INIT "${lp64_flags}")
set(CMAKE_CXX_FLAGS_INIT "${lp64_flags}")
set(lp64_link "-Wl,-Ttext-segment=${LP64_IMAGE_BASE} -Wl,--defsym=lp64_p32_base=__executable_start -Wl,-z,separate-code")
if(LP64_STATIC)
  string(PREPEND lp64_link "-static ") # non-PIE already
else()
  string(PREPEND lp64_link "-no-pie ")
endif()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${lp64_link}")
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
