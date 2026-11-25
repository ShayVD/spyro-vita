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

set(SPYRO_HOST "linux32" CACHE STRING "Host to build: linux32, linux64, vita, ps4 or pc")
# linux64 (docs/ps4-plan.md P1, decisions D174): the headless host of
# src/platform/linux32 built for x86-64 with clang, the PS4's data model, in
# the low-image model (D173): pointer fields P32 (src/compat/p32.h), the
# arena and the game thread's stack in the image, below 2 GB.
if(SPYRO_HOST STREQUAL "linux32" OR SPYRO_HOST STREQUAL "linux64")
  set(SPYRO_HOST_LINUX ON)
  set(SPYRO_HOST_DIR linux32)
else()
  set(SPYRO_HOST_LINUX OFF)
  set(SPYRO_HOST_DIR ${SPYRO_HOST})
endif()
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
# The port's own patches, then the region switches it shares with the
# matching builds (tools/build_matching.sh REGION=..., D133), then the region
# edits on lines the port's patches rewrote (the matching builds have theirs
# in patches/spyro-1-region-matching; D135, tools/regions/retune.py patch),
# then the 60 Hz rules' class sites written on the region's text (D144).
spyro_prepare(spyro-1 "${CMAKE_SOURCE_DIR}/third_party/spyro-1"
              "${CMAKE_SOURCE_DIR}/patches/spyro-1-port;${CMAKE_SOURCE_DIR}/patches/spyro-1-region;${CMAKE_SOURCE_DIR}/patches/spyro-1-region-port;${CMAKE_SOURCE_DIR}/patches/spyro-1-tick60;${CMAKE_SOURCE_DIR}/patches/spyro-1-ptr32;${CMAKE_SOURCE_DIR}/patches/spyro-1-p32off" S1_DIR)
spyro_prepare(PsyCross "${CMAKE_SOURCE_DIR}/third_party/PsyCross"
              "${CMAKE_SOURCE_DIR}/patches/PsyCross" PSYCROSS_DIR)

set(GEN_DIR "${CMAKE_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${GEN_DIR}")

# Flags that guard behaviour on every target (decisions D6).
set(SPYRO_GUARD_FLAGS -funsigned-char -fno-strict-aliasing -ffp-contract=off)
# P32 (32-bit pointer fields on x86-64, empty elsewhere) in every C and C++
# unit, and in the overlays' preprocessing (cmake/overlays.cmake; D174).
set(SPYRO_P32_H "${CMAKE_SOURCE_DIR}/src/compat/p32.h")
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-include ${SPYRO_P32_H}>")
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
elseif(SPYRO_HOST STREQUAL "ps4")
  # The PS4 (docs/ps4-plan.md P3, D193): linux64's flags for OpenOrbis's
  # FreeBSD target (cmake/ps4.toolchain.cmake), the sysroot spelt out for the
  # overlays' preprocessing, which calls the compiler directly. The eboot is
  # a PIE (OpenOrbis's link.x; the PS4 loads it at 0x400000, D180), so the
  # code is -fPIE, not -fno-pic.
  set(SPYRO_ARCH_FLAGS --target=x86_64-pc-freebsd12-elf --sysroot=${OO_PS4_TOOLCHAIN}
      -isystem ${OO_PS4_TOOLCHAIN}/include -fms-extensions -fPIE -march=btver2)
  # No instruction that faults on a misaligned address: the decomp's arrays
  # sit at their PS1 addresses, but clang takes a global array of 16 bytes
  # or more as 16-aligned (tools/ps4/cc_unaligned.py; hardware run 2).
  set(CMAKE_C_COMPILER_LAUNCHER "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/ps4/cc_unaligned.py")
  # -fno-vectorize: with AVX, clang 18's loop vectorizer turns P32_INIT's
  # copy loops into a vector addrspacecast its backend can't select
  # ("Cannot select: v4i32 = addrspacecast", a fourth __ptr32 bug, D193).
  set(SPYRO_OPT_FLAGS -O2 -g -fno-vectorize -Wno-error=implicit-function-declaration
      -Wno-error=implicit-int -Wno-error=incompatible-function-pointer-types -Wno-error=return-type)
  set(SPYRO_PIC_FLAGS -fPIE)
elseif(SPYRO_HOST STREQUAL "pc")
  # The desktop build (docs/pc-build.md, D2xx): linux64's model and flags for
  # x86-64 Linux or Windows (cmake/pc-linux.toolchain.cmake,
  # pc-windows.toolchain.cmake set SPYRO_PC_TARGET), optimised. -fno-vectorize:
  # the PS4's clang 18 vectorized P32_INIT's loops into an addrspacecast its
  # backend can't select (D193); kept off for every clang here.
  set(SPYRO_ARCH_FLAGS --target=${SPYRO_PC_TARGET} -fms-extensions)
  if(SPYRO_PC_TARGET MATCHES "mingw")
    # Windows (D241): the GNU bitfield layout, not Microsoft's (the PS1's
    # structs keep their offsets); src/compat/p32.h brings pc_win.h
    list(APPEND SPYRO_ARCH_FLAGS -mno-ms-bitfields)
    set(SPYRO_COFF_ARG --coff) # tools/data2s.py, pres_snap_syms.py
    # PsyQ's long is Windows' long (32 bits), but clang won't take a long *
    # for an int * as an error there: the same bytes, a warning
    set(SPYRO_PC_EXTRA_FLAGS -Wno-error=incompatible-pointer-types)
  endif()
  set(SPYRO_OPT_FLAGS -O2 -g -fno-vectorize -Wno-error=implicit-function-declaration
      -Wno-error=implicit-int -Wno-error=incompatible-function-pointer-types -Wno-error=return-type
      ${SPYRO_PC_EXTRA_FLAGS})
elseif(SPYRO_HOST STREQUAL "linux64")
  # --target here too: the overlays' preprocessing and the relocatable links
  # (cmake/overlays.cmake, presenter.cmake, tick60.cmake) call the compiler
  # directly. -fms-extensions: __ptr32 (src/compat/p32.h). clang makes these
  # errors; GCC (linux32) takes them as warnings, and they don't depend on
  # the pointer's size. int-conversion stays an error: those sites are read.
  set(SPYRO_ARCH_FLAGS --target=x86_64-linux-gnu -fms-extensions)
  set(SPYRO_OPT_FLAGS -O1 -g -Wno-error=implicit-function-declaration -Wno-error=implicit-int
      -Wno-error=incompatible-function-pointer-types -Wno-error=return-type)
else()
  set(SPYRO_ARCH_FLAGS -m32)
  set(SPYRO_OPT_FLAGS -O1 -g)
endif()
if(NOT DEFINED SPYRO_PIC_FLAGS)
  set(SPYRO_PIC_FLAGS -fno-pic)
endif()
# The executable's link: static and non-PIE at 0x400000 on linux64, so the
# whole image is low (D174); -m32 -no-pie on linux32.
if(SPYRO_HOST STREQUAL "linux64")
  set(SPYRO_LINK_FLAGS ${SPYRO_ARCH_FLAGS} -static) # static: non-PIE (objects -fno-pic)
  set(SPYRO_RELOC_LD_FLAGS ${SPYRO_ARCH_FLAGS})
elseif(SPYRO_HOST STREQUAL "pc")
  # dynamic (SDL loads the system's display, GL and sound libraries) and
  # non-PIE at 0x400000 on Linux; a fixed base of 0x400000 on Windows
  # (src/platform/pc/CMakeLists.txt)
  set(SPYRO_LINK_FLAGS ${SPYRO_ARCH_FLAGS})
  set(SPYRO_RELOC_LD_FLAGS ${SPYRO_ARCH_FLAGS} ${SPYRO_PC_RELOC_LD_FLAGS})
elseif(SPYRO_HOST STREQUAL "ps4")
  # the presenter's and tick60's relocatable links (the eboot's own link is
  # src/platform/ps4's, ld.lld with OpenOrbis's link.x)
  set(SPYRO_RELOC_LD_FLAGS ${SPYRO_ARCH_FLAGS} -fuse-ld=${PS4_LD})
elseif(SPYRO_HOST STREQUAL "linux32")
  set(SPYRO_LINK_FLAGS -m32 -no-pie)
  set(SPYRO_RELOC_LD_FLAGS -m32)
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
# 2 GB too; its data labels are ordinary globals and the game's memory one
# block from the heap (D106). The Vita's default since D106's hardware run
# (-DSPYRO_NATIVE_MEM=OFF: the moved arena); linux32's stays the arena build,
# the oracle replay's.
if(SPYRO_HOST MATCHES "^(vita|linux64|ps4|pc)$")
  set(native_mem_default ON)
else()
  set(native_mem_default OFF)
endif()
option(SPYRO_NATIVE_MEM "Native memory: no mips2c units, no aliases of PS1 RAM, any base (D100, D106)"
       ${native_mem_default})
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
# The low image (D173, D174): the arena and the game thread's stack are
# .bss arrays in the executable, which must lie below 2 GB (the decomp's
# (int) casts sign-extend); a start-up check refuses to run otherwise.
# linux64 (D174), the PS4 (D193) and the PC (D2xx).
if(SPYRO_HOST MATCHES "^(linux64|ps4|pc)$")
  if(NOT SPYRO_NATIVE_MEM)
    message(FATAL_ERROR "${SPYRO_HOST} needs SPYRO_NATIVE_MEM=ON (the low image, D174)")
  endif()
  set(SPYRO_LOW_IMAGE ON)
  add_compile_definitions(SPYRO_LOW_IMAGE=1)
else()
  set(SPYRO_LOW_IMAGE OFF)
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

# linux32: every link goes through cmake/link_exec.sh, which makes sure an
# executable keeps its execute bit: ld's own chmod of its output is now and
# then lost on tools/tc's bind mount (colima's reverse-sshfs; issue #163).
if(SPYRO_HOST STREQUAL "linux32")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK
    "sh ${CMAKE_SOURCE_DIR}/cmake/link_exec.sh <TARGET> --")
endif()

add_subdirectory(src/platform/${SPYRO_HOST_DIR})

if(SPYRO_HOST STREQUAL "linux32")
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(tools) # pbp_extract (D157)
endif()
