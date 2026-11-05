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

# The game's PS1 layout comes from the matching build's psx.map. Without a
# matching build (CI: it needs the user's own PsyQ headers) the build reads
# the reduced copy in tools/layout instead (tools/reduce_map.py, decisions D57).
set(SPYRO_PSX_MAP "${SPYRO_MATCHING_DIR}/build/psx.map")
set(SPYRO_PSX_MAP_REDUCED "${CMAKE_SOURCE_DIR}/tools/layout/psx.map")
if(NOT EXISTS "${SPYRO_PSX_MAP}")
  set(SPYRO_PSX_MAP "${SPYRO_PSX_MAP_REDUCED}")
  message(STATUS "No matching build: PS1 layout from ${SPYRO_PSX_MAP}")
else()
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/reduce_map.py"
    --check "${SPYRO_PSX_MAP}" "${SPYRO_PSX_MAP_REDUCED}" RESULT_VARIABLE stale)
  if(stale)
    message(WARNING "tools/layout/psx.map differs from the matching build's: CI builds a different layout")
  endif()
endif()

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
# toolchain file picks the CPU (decisions D25).
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

# Where the PS1 RAM arena sits (docs/porting-plan.md §4.3, decisions D25,
# D97). linux32 maps it at 0x80000000 itself by default: the game's data
# labels are then absolute symbols at their PS1 addresses (D23) and the
# oracle replay compares RAM byte for byte. The Vita can't ask for that
# address, so its arena moves: data labels through pointers, three aliases
# of PS1 RAM in translated code (D25). SPYRO_ARENA_AT, another 16 MB-aligned
# address above 2 GB, runs that moved model on linux32 too, where the host's
# checks can test it (D97).
# The native build (docs/native-plan.md stage 4, decisions D97, D100): no
# mips2c unit linked, so no aliases of PS1 RAM (MIPS_KUSEG and HPTR are plain
# casts); every packed or tagged address decodes in src/game/psx_mem.h,
# relative to the arena's base, so SPYRO_ARENA_AT may be any page, below
# 2 GB too. The oracle replay stays on the arena build.
option(SPYRO_NATIVE_MEM "Native memory: no mips2c units, no aliases of PS1 RAM, any base (D100)" OFF)
set(SPYRO_ARENA_AT "0x80000000" CACHE STRING
    "linux32: the arena's host address; not 0x80000000 = the Vita's moved arena (D97)")
# The native build's data labels are ordinary globals (D106): no moved
# arena's label pointers, whatever SPYRO_ARENA_AT says.
if(SPYRO_HOST STREQUAL "vita")
  if(SPYRO_NATIVE_MEM)
    set(SPYRO_ARENA_MOVED OFF)
  else()
    set(SPYRO_ARENA_MOVED ON)
  endif()
elseif(SPYRO_ARENA_AT MATCHES "^0x[0-9A-Fa-f]+$")
  math(EXPR arena_at "${SPYRO_ARENA_AT}" OUTPUT_FORMAT HEXADECIMAL)
  if(SPYRO_NATIVE_MEM)
    # the native build (D100) needs no alignment and no bit 31: a page
    math(EXPR arena_low "${SPYRO_ARENA_AT} & 0xFFF" OUTPUT_FORMAT HEXADECIMAL)
    if(NOT arena_low STREQUAL "0x0")
      message(FATAL_ERROR "SPYRO_ARENA_AT ${SPYRO_ARENA_AT}: needs page alignment")
    endif()
  else()
    math(EXPR arena_low "${SPYRO_ARENA_AT} & 0x80FFFFFF" OUTPUT_FORMAT HEXADECIMAL)
    if(NOT arena_low STREQUAL "0x80000000")
      message(FATAL_ERROR "SPYRO_ARENA_AT ${SPYRO_ARENA_AT}: needs 16 MB alignment and bit 31")
    endif()
  endif()
  if(arena_at STREQUAL "0x80000000")
    set(SPYRO_ARENA_MOVED OFF)
  else()
    set(SPYRO_ARENA_MOVED ON)
    add_compile_definitions(PSX_ARENA_AT=${SPYRO_ARENA_AT}u)
  endif()
  if(SPYRO_NATIVE_MEM)
    set(SPYRO_ARENA_MOVED OFF)
  endif()
else()
  message(FATAL_ERROR "SPYRO_ARENA_AT ${SPYRO_ARENA_AT}: not a hex address")
endif()
if(SPYRO_ARENA_MOVED)
  add_compile_definitions(SPYRO_ARENA_MOVED=1)
endif()
if(SPYRO_NATIVE_MEM)
  add_compile_definitions(SPYRO_NATIVE_MEM=1)
endif()
# Debug: list the translated and hand C's accesses through an alias of PS1
# RAM (mips2c_rt.h MIPS_KUSEG; decisions D100).
# Debug: list the decodes of packed addresses (OT links, frame pointers)
# that land outside overlay space, the native build's block (D106).
option(SPYRO_MEM_AUDIT "Log every packed address decoded outside overlay space" OFF)
if(SPYRO_MEM_AUDIT)
  add_compile_definitions(SPYRO_MEM_AUDIT=1)
endif()
option(SPYRO_KUSEG_AUDIT "Log every site where MIPS_KUSEG maps an alias" OFF)
if(SPYRO_KUSEG_AUDIT)
  add_compile_definitions(SPYRO_KUSEG_AUDIT=1)
endif()

include(cmake/psycross.cmake)
include(cmake/platform_common.cmake)
include(cmake/game.cmake)

add_subdirectory(src/platform/${SPYRO_HOST})

if(SPYRO_HOST STREQUAL "linux32")
  enable_testing()
  add_subdirectory(tests)
endif()
