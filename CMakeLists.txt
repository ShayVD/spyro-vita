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

include(cmake/psycross.cmake)
include(cmake/platform_common.cmake)
include(cmake/game.cmake)

add_subdirectory(src/platform/${SPYRO_HOST})

if(SPYRO_HOST STREQUAL "linux32")
  enable_testing()
  add_subdirectory(tests)
endif()
