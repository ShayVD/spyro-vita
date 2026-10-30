# Spyro the Dragon (SCUS-94228) native port. See docs/porting-plan.md.
#   tools/tc cmake -S . -B build/linux32 -G Ninja -DSPYRO_HOST=linux32 \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/linux32.toolchain.cmake
#   tools/tc cmake --build build/linux32
# Generated C goes to ${CMAKE_BINARY_DIR}/gen and is never committed.
cmake_minimum_required(VERSION 3.20)
project(spyro C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)

set(SPYRO_HOST "linux32" CACHE STRING "Host to build: linux32 or vita")
set(SPYRO_MATCHING_DIR "${CMAKE_SOURCE_DIR}/build/s1-matching" CACHE PATH
    "tools/build_matching.sh output (psx.map, psx.elf, wad/*.ovl)")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(cmake/prepare.cmake)
spyro_prepare(spyro-1 "${CMAKE_SOURCE_DIR}/third_party/spyro-1"
              "${CMAKE_SOURCE_DIR}/patches/spyro-1-port" S1_DIR)
spyro_prepare(PsyCross "${CMAKE_SOURCE_DIR}/third_party/PsyCross"
              "${CMAKE_SOURCE_DIR}/patches/PsyCross" PSYCROSS_DIR)

set(GEN_DIR "${CMAKE_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${GEN_DIR}")

# Flags that guard behaviour on every target (decisions D6).
set(SPYRO_GUARD_FLAGS -funsigned-char -fno-strict-aliasing -ffp-contract=off)
# Code generation per host: the linux32 debug build is -m32 -O1; the Vita's
# toolchain file picks the CPU (decisions D24).
if(SPYRO_HOST STREQUAL "vita")
  set(SPYRO_ARCH_FLAGS)
  # GCC 15 makes these errors; the decomp's C (written for GCC 2.7.2) and
  # the i686 GCC of the linux32 build take them as warnings.
  set(SPYRO_OPT_FLAGS -O2 -g -Wno-error=int-conversion
      -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration
      -Wno-error=implicit-int -Wno-error=return-mismatch
      # save_file.c's SaveCreate writes past m_CollectedEggs as the original
      # does; at -O2 GCC would otherwise rewrite the loop around that UB.
      -fno-aggressive-loop-optimizations)
else()
  set(SPYRO_ARCH_FLAGS -m32)
  set(SPYRO_OPT_FLAGS -O1 -g)
endif()

include(cmake/psycross.cmake)
include(cmake/platform_common.cmake)
include(cmake/game.cmake)

add_subdirectory(src/platform/${SPYRO_HOST})

if(SPYRO_HOST STREQUAL "linux32")
  enable_testing()
  add_subdirectory(tests)
endif()
