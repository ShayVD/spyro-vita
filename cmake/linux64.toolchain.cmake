# x86-64 Linux with clang, used inside tools/tc (tools/docker/host.Dockerfile):
# the PS4's data model on the host (docs/ps4-plan.md P1, decisions D173, D174).
# clang for __ptr32 (src/compat/p32.h); the cross libc, libgcc and binutils
# come with gcc-x86-64-linux-gnu. The binary is static and non-PIE at
# 0x400000 (src/platform/linux32/CMakeLists.txt) and runs under qemu-x86_64.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(SPYRO_CLANG clang-19 CACHE STRING "clang for linux64 (clang-16 also in the image)")
string(REPLACE "clang" "clang++" spyro_clangxx "${SPYRO_CLANG}")
set(CMAKE_C_COMPILER ${SPYRO_CLANG})
set(CMAKE_CXX_COMPILER ${spyro_clangxx})
set(CMAKE_ASM_COMPILER ${SPYRO_CLANG})
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
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-x86_64)
# clang 19 hangs (16 crashes) on a ?: that mixes a __ptr32 and a normal
# pointer (src/compat/p32.h, D174): no compile may run for ever.
set(CMAKE_C_COMPILER_LAUNCHER timeout 300)
