# x86-64 Linux with clang: a 4-byte-pointer game's 64-bit host build in the low-image model (lp64/docs/low-image.md),
# used inside the host container (lp64/cmake/host-lp64.Dockerfile, the game's tools/tc):
#   tools/tc cmake -S . -B build/linux64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=<kit>/lp64/cmake/linux64.toolchain.cmake
# clang for __ptr32 (lp64/runtime/p32); the x86-64 cross libc, libgcc and binutils come with gcc-x86-64-linux-gnu.
# What the model needs from every unit and the link is set here:
#  - -fms-extensions (__ptr32) and lp64_p32.h forced into every C and C++ unit (LP64_P32_H; empty to leave it out);
#  - objects -fno-pic, and the executable -static, which is non-PIE at 0x400000: the whole image low;
#  - every compile under `timeout 300`: clang 19 hangs on a ?: mixing a __ptr32 and a normal pointer
#    (lp64/docs/clang-bugs.md), and no compile may run for ever.
# A game's own flags (warnings clang makes errors, its defines) stay in its CMakeLists.txt; spyro-vita's are in
# lp64/docs/low-image.md "The build".
# On a non-x86-64 machine the binaries run under qemu-x86_64 (CMAKE_CROSSCOMPILING_EMULATOR); on x86-64 Linux
# directly.
# From spyro-vita cmake/linux64.toolchain.cmake and CMakeLists.txt's linux64 flags @ c25a3ea (MIT; D174). Its
# SPYRO_CLANG is LP64_CLANG.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(LP64_CLANG clang-19 CACHE STRING "clang for the 64-bit build (any clang that targets x86_64-linux-gnu)")
set(LP64_P32_H "${CMAKE_CURRENT_LIST_DIR}/../runtime/p32/lp64_p32.h" CACHE FILEPATH
    "forced into every C and C++ unit (empty: leave it to the game)")
string(REPLACE "clang" "clang++" lp64_clangxx "${LP64_CLANG}")
set(CMAKE_C_COMPILER ${LP64_CLANG})
set(CMAKE_CXX_COMPILER ${lp64_clangxx})
set(CMAKE_ASM_COMPILER ${LP64_CLANG})
set(CMAKE_C_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_ASM_COMPILER_TARGET x86_64-linux-gnu)
set(CMAKE_AR x86_64-linux-gnu-ar CACHE FILEPATH "")
set(CMAKE_RANLIB x86_64-linux-gnu-ranlib CACHE FILEPATH "")
set(CMAKE_NM x86_64-linux-gnu-nm CACHE FILEPATH "")
set(CMAKE_OBJCOPY x86_64-linux-gnu-objcopy CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
if(NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  set(CMAKE_CROSSCOMPILING_EMULATOR qemu-x86_64)
endif()
set(lp64_flags "-fms-extensions -fno-pic")
if(LP64_P32_H)
  string(APPEND lp64_flags " -include ${LP64_P32_H}")
endif()
set(CMAKE_C_FLAGS_INIT "${lp64_flags}")
set(CMAKE_CXX_FLAGS_INIT "${lp64_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
set(CMAKE_C_COMPILER_LAUNCHER timeout 300)
set(CMAKE_CXX_COMPILER_LAUNCHER timeout 300)
