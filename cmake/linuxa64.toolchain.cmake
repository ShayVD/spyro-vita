# aarch64 Linux with clang, native in the arm64 host image (tools/tc,
# tools/docker/host.Dockerfile): the offset model's headless host (D250 O3).
# Encore's toolchain (third_party/encore/lp64/cmake/linuxa64.toolchain.cmake,
# D279): static, non-PIE at 0x2_0123_0000, the base the image's start. spyro
# forces its own src/compat/p32.h (CMakeLists.txt), so not the kit's header;
# SPYRO_CLANG is the old name of LP64_CLANG.
set(LP64_P32_H "" CACHE FILEPATH "spyro forces src/compat/p32.h (CMakeLists.txt)")
if(SPYRO_CLANG)
  set(LP64_CLANG "${SPYRO_CLANG}" CACHE STRING "clang for linuxa64")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../third_party/encore/lp64/cmake/linuxa64.toolchain.cmake")
