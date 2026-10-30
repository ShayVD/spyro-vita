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
  asm/nonmatchings/pete/func_80048D10.s
  asm/nonmatchings/pete/func_80049FAC.s)
# A/B tests: functions listed here (or "all") build from the mips2c unit
# instead, and their hand C compiles out (M2C_<name>=1).
set(SPYRO_M2C_FUNCS "" CACHE STRING
  "Hand-written functions to take from mips2c instead (names, or all)")
set(M2C_HAND_DEFS)
foreach(u ${M2C_HAND})
  get_filename_component(f "${u}" NAME_WE)
  if(SPYRO_M2C_FUNCS STREQUAL "all" OR f IN_LIST SPYRO_M2C_FUNCS)
    list(APPEND M2C_HAND_DEFS M2C_${f}=1)
  else()
    list(APPEND M2C_HAND_DEFS M2C_${f}=0)
    list(REMOVE_ITEM M2C_UNITS ${u})
  endif()
endforeach()

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
    --out-dir "${GEN_DIR}/mips2c" --hints "${M2C_HINTS}"
    --index "${GEN_DIR}/mips2c/m2c_index.c" ${M2C_UNITS}
  DEPENDS ${M2C_DEPS}
  COMMENT "mips2c: translating decomp asm to C")

add_library(spyro_game_m2c OBJECT ${M2C_OUTS})
# Register seeding for the oracle replay (spyro --replay): host debug build only.
if(SPYRO_HOST STREQUAL "linux32")
  set(M2C_REPLAY_DEF MIPS2C_REPLAY=1)
else()
  set(M2C_REPLAY_DEF MIPS2C_REPLAY=0)
endif()
target_compile_definitions(spyro_game_m2c PRIVATE ${M2C_REPLAY_DEF})
target_include_directories(spyro_game_m2c PRIVATE "${CMAKE_SOURCE_DIR}/src/game")
target_compile_options(spyro_game_m2c PRIVATE ${SPYRO_HOST_FLAGS} -w)

file(GLOB PORT_SRCS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/game/*.c")
add_library(spyro_game_port OBJECT ${PORT_SRCS})
target_compile_definitions(spyro_game_port PRIVATE ${M2C_REPLAY_DEF} ${M2C_HAND_DEFS})
target_include_directories(spyro_game_port PRIVATE
  "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/platform/common")
# trace.c reads game state through the decomp's headers.
target_include_directories(spyro_game_port AFTER PRIVATE
  "${CMAKE_SOURCE_DIR}/src/compat" "${S1_DIR}/include")
target_link_libraries(spyro_game_port PRIVATE psycross)
target_compile_options(spyro_game_port PRIVATE ${SPYRO_HOST_FLAGS})
