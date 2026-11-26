# aarch64 Linux with clang, native in the arm64 host image (tools/tc,
# tools/docker/host.Dockerfile): the offset model's headless host (D250 O3).
# src/compat/p32.h turns SPYRO_P32_OFFSET on for aarch64, so every P32 slot
# is a 32-bit offset from spyro_p32_base. The binary is static and non-PIE,
# linked above 4 GB as macOS arm64 places its image (CMakeLists.txt), and
# runs directly: no qemu.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(SPYRO_CLANG clang-19 CACHE STRING "clang for linuxa64")
string(REPLACE "clang" "clang++" spyro_clangxx "${SPYRO_CLANG}")
set(CMAKE_C_COMPILER ${SPYRO_CLANG})
set(CMAKE_CXX_COMPILER ${spyro_clangxx})
set(CMAKE_ASM_COMPILER ${SPYRO_CLANG})
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_ASM_COMPILER_TARGET aarch64-linux-gnu)
