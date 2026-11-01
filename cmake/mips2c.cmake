# Assembly the decomp hasn't matched to C, translated by tools/mips2c
# (docs/porting-plan.md §7.2), plus our hand-written C in src/game.
set(M2C_TOOL "${CMAKE_SOURCE_DIR}/tools/mips2c/mips2c.py")
set(M2C_HINTS "${CMAKE_SOURCE_DIR}/tools/mips2c/hints.yaml")
# Hand-written in src/game instead: memory.s, order_table.s, system_time.s.
set(M2C_UNITS
  asm/math.s asm/42CC4.s asm/moby_lists.s asm/particle_alloc.s asm/images.s
  asm/collision.s
  asm/renderers/r_moby.s asm/renderers/r_environment.s
  asm/renderers/r_cyclorama.s asm/renderers/r_pete.s
  asm/renderers/r_particles.s asm/renderers/r_shadows.s
  asm/renderers/r_flame.s)
file(GLOB_RECURSE m2c_nonmatching RELATIVE "${S1_DIR}" CONFIGURE_DEPENDS
  "${S1_DIR}/asm/nonmatchings/*.s")
list(SORT m2c_nonmatching)
list(APPEND M2C_UNITS ${m2c_nonmatching})
# Replaced by hand-written C in src/game (docs/translation-log.md); the
# oracle replay finds them through src/game/hand_index.c.
set(M2C_HAND
  asm/nonmatchings/camera/func_80033F08.s
  asm/nonmatchings/camera/func_80034480.s
  asm/nonmatchings/camera/func_80034CE8.s
  asm/nonmatchings/camera/func_800357A4.s
  asm/nonmatchings/camera/func_80035FB4.s
  asm/nonmatchings/pete/func_8003EA68.s
  asm/nonmatchings/pete/func_8003FE40.s
  asm/nonmatchings/pete/func_80041670.s
  asm/nonmatchings/pete/func_80043FE4.s
  asm/nonmatchings/pete/func_80047B60.s
  asm/nonmatchings/pete/func_80048D10.s
  asm/nonmatchings/pete/func_80049FAC.s)
# A/B tests: functions listed here (or "all") build from the mips2c unit
# instead, and their hand C compiles out (M2C_<name>=1).
set(SPYRO_M2C_FUNCS "" CACHE STRING
  "Hand-written functions to take from mips2c instead (names, or all)")
# Differential sweeps (oracle_replay.c SPYRO_REPLAY_SWEEP) need a reference:
# SPYRO_M2C_REF=ON also links each hand-written function's mips2c unit,
# renamed <name>_gen, and lists it in hand_index.c's m2c_gen_index.
option(SPYRO_M2C_REF "Link the mips2c units of hand-written functions as <name>_gen" OFF)
set(M2C_HAND_DEFS)
set(M2C_REF_UNITS)
foreach(u ${M2C_HAND})
  get_filename_component(f "${u}" NAME_WE)
  if(SPYRO_M2C_FUNCS STREQUAL "all" OR f IN_LIST SPYRO_M2C_FUNCS)
    list(APPEND M2C_HAND_DEFS M2C_${f}=1)
  else()
    list(APPEND M2C_HAND_DEFS M2C_${f}=0)
    list(REMOVE_ITEM M2C_UNITS ${u})
    if(SPYRO_M2C_REF)
      list(APPEND M2C_REF_UNITS ${u})
    endif()
  endif()
endforeach()
if(SPYRO_M2C_REF)
  list(APPEND M2C_HAND_DEFS M2C_REF=1)
else()
  list(APPEND M2C_HAND_DEFS M2C_REF=0)
endif()

# Partly hand-written: the hand C handles what it covers and calls the mips2c
# unit, renamed <name>_gen, for the rest (docs/translation-log.md). None at
# the moment (func_80043FE4 was, until its flying-ability phases).
set(M2C_PARTIAL)
set(M2C_PARTIAL_DEFS)
foreach(u ${M2C_PARTIAL})
  get_filename_component(f "${u}" NAME_WE)
  if(SPYRO_M2C_FUNCS STREQUAL "all" OR f IN_LIST SPYRO_M2C_FUNCS)
    list(APPEND M2C_HAND_DEFS M2C_${f}=1)
  else()
    list(APPEND M2C_HAND_DEFS M2C_${f}=0)
    string(REGEX REPLACE "^asm/" "" n "${u}")
    string(REGEX REPLACE "\\.s$" "" n "${n}")
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" n "${n}")
    list(APPEND M2C_PARTIAL_DEFS "${GEN_DIR}/mips2c/m2c_${n}.c"
      "${f}=${f}_gen|${f}__m2c=${f}_gen__m2c")
  endif()
endforeach()

# Profiling builds: every translated instruction counts its executions
# (mips2c --prof, M2C_PROF in mips2c_rt.h; tools/mips2c/prof_report.py).
option(SPYRO_M2C_PROF "Count executions of every translated instruction" OFF)
set(M2C_PROF_ARG)
set(M2C_PROF_DEF M2C_PROFILE=0)
if(SPYRO_M2C_PROF)
  set(M2C_PROF_ARG --prof)
  set(M2C_PROF_DEF M2C_PROFILE=1)
endif()

set(M2C_OUTS)
set(M2C_DEPS "${M2C_TOOL}" "${M2C_HINTS}")
foreach(u ${M2C_UNITS})
  string(REGEX REPLACE "^asm/" "" n "${u}")
  string(REGEX REPLACE "\\.s$" "" n "${n}")
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" n "${n}")
  list(APPEND M2C_OUTS "${GEN_DIR}/mips2c/m2c_${n}.c")
  list(APPEND M2C_DEPS "${S1_DIR}/${u}")
endforeach()
list(APPEND M2C_OUTS "${GEN_DIR}/mips2c/m2c_index.c")
add_custom_command(OUTPUT ${M2C_OUTS}
  COMMAND "${Python3_EXECUTABLE}" "${M2C_TOOL}" --root "${S1_DIR}"
    --out-dir "${GEN_DIR}/mips2c" --hints "${M2C_HINTS}" ${M2C_PROF_ARG}
    --index "${GEN_DIR}/mips2c/m2c_index.c" ${M2C_UNITS}
  DEPENDS ${M2C_DEPS}
  COMMENT "mips2c: translating decomp asm to C")

# The reference units go to their own directory, with an index nothing
# compiles: m2c_index.c must not name their entry points. One mips2c run per
# unit, so their calls to other hand-written functions take the C entry (a
# batch would call its own units' register entries, <name>__m2c).
set(M2C_REF_OUTS)
foreach(u ${M2C_REF_UNITS})
  get_filename_component(f "${u}" NAME_WE)
  string(REGEX REPLACE "^asm/" "" n "${u}")
  string(REGEX REPLACE "\\.s$" "" n "${n}")
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" n "${n}")
  set(o "${GEN_DIR}/mips2c-ref/m2c_${n}.c")
  list(APPEND M2C_REF_OUTS "${o}")
  list(APPEND M2C_PARTIAL_DEFS "${o}" "${f}=${f}_gen|${f}__m2c=${f}_gen__m2c")
  add_custom_command(OUTPUT "${o}"
    COMMAND "${Python3_EXECUTABLE}" "${M2C_TOOL}" --root "${S1_DIR}"
      --out-dir "${GEN_DIR}/mips2c-ref" --hints "${M2C_HINTS}"
      --index "${GEN_DIR}/mips2c-ref/${f}_index.c" ${u}
    DEPENDS "${M2C_TOOL}" "${M2C_HINTS}" "${S1_DIR}/${u}"
    COMMENT "mips2c: ${f} as a reference for differential sweeps")
endforeach()

# Hand-written renderers (src/game/render_hand.h HAND_RENDERER, decisions
# D69): the mips2c unit stays linked with the function's two entries renamed
# NAME_gen and NAME_gen__m2c, and fast_render (config.txt, host
# --fast-render) picks the hand C or the unit at each call.
set(M2C_RENDER_HAND
  asm/renderers/r_cyclorama.s:func_8004EBA8)
set(M2C_RENDER_FILES)
foreach(e ${M2C_RENDER_HAND})
  string(REPLACE ":" ";" e "${e}")
  list(GET e 0 u)
  list(GET e 1 f)
  string(REGEX REPLACE "^asm/" "" n "${u}")
  string(REGEX REPLACE "\\.s$" "" n "${n}")
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" n "${n}")
  set(src "${GEN_DIR}/mips2c/m2c_${n}.c")
  list(APPEND M2C_RENDER_FILES "${src}")
  list(APPEND M2C_RENDER_DEFS_${n} "${f}=${f}_gen" "${f}__m2c=${f}_gen__m2c")
endforeach()
list(REMOVE_DUPLICATES M2C_RENDER_FILES)

add_library(spyro_game_m2c OBJECT ${M2C_OUTS} ${M2C_REF_OUTS})
foreach(src ${M2C_RENDER_FILES})
  get_filename_component(n "${src}" NAME_WE)
  string(REGEX REPLACE "^m2c_" "" n "${n}")
  set_source_files_properties("${src}" TARGET_DIRECTORY spyro_game_m2c
    PROPERTIES COMPILE_DEFINITIONS "${M2C_RENDER_DEFS_${n}}")
endforeach()
while(M2C_PARTIAL_DEFS)
  list(POP_FRONT M2C_PARTIAL_DEFS src defs)
  string(REPLACE "|" ";" defs "${defs}")
  set_source_files_properties("${src}" TARGET_DIRECTORY spyro_game_m2c
    PROPERTIES COMPILE_DEFINITIONS "${defs}")
endwhile()
# Register seeding for the oracle replay (spyro --replay): host debug build only.
if(SPYRO_HOST STREQUAL "linux32")
  set(M2C_REPLAY_DEF MIPS2C_REPLAY=1)
else()
  set(M2C_REPLAY_DEF MIPS2C_REPLAY=0)
endif()
target_compile_definitions(spyro_game_m2c PRIVATE ${M2C_REPLAY_DEF} ${M2C_PROF_DEF})
target_include_directories(spyro_game_m2c PRIVATE "${CMAKE_SOURCE_DIR}/src/game"
  "${PSYCROSS_DIR}/include/psx") # gtereg.h: mips2c_rt.h's inline GTE moves
target_compile_options(spyro_game_m2c PRIVATE ${SPYRO_HOST_FLAGS} -w)

file(GLOB PORT_SRCS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/game/*.c")
add_library(spyro_game_port OBJECT ${PORT_SRCS})
target_compile_definitions(spyro_game_port PRIVATE ${M2C_REPLAY_DEF} ${M2C_HAND_DEFS} ${M2C_PROF_DEF})
target_include_directories(spyro_game_port PRIVATE
  "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/platform/common")
# trace.c reads game state through the decomp's headers.
target_include_directories(spyro_game_port AFTER PRIVATE
  "${CMAKE_SOURCE_DIR}/src/compat" "${S1_DIR}/include")
target_link_libraries(spyro_game_port PRIVATE psycross)
target_compile_options(spyro_game_port PRIVATE ${SPYRO_HOST_FLAGS})
# Save states refuse states from another build (src/game/savestate.c, D51).
execute_process(COMMAND git describe --always --dirty
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE SPYRO_GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT SPYRO_GIT_DESCRIBE)
  set(SPYRO_GIT_DESCRIBE unknown)
endif()
set_source_files_properties("${CMAKE_SOURCE_DIR}/src/game/savestate.c" TARGET_DIRECTORY
  spyro_game_port PROPERTIES COMPILE_DEFINITIONS "SPYRO_BUILD_ID=\"${SPYRO_GIT_DESCRIBE}\"")
