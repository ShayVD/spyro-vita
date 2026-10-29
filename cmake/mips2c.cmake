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

set(M2C_OUTS)
set(M2C_DEPS "${M2C_TOOL}" "${M2C_HINTS}")
foreach(u ${M2C_UNITS})
  string(REGEX REPLACE "^asm/" "" n "${u}")
  string(REGEX REPLACE "\\.s$" "" n "${n}")
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" n "${n}")
  list(APPEND M2C_OUTS "${GEN_DIR}/mips2c/m2c_${n}.c")
  list(APPEND M2C_DEPS "${S1_DIR}/${u}")
endforeach()
add_custom_command(OUTPUT ${M2C_OUTS}
  COMMAND "${Python3_EXECUTABLE}" "${M2C_TOOL}" --root "${S1_DIR}"
    --out-dir "${GEN_DIR}/mips2c" --hints "${M2C_HINTS}" ${M2C_UNITS}
  DEPENDS ${M2C_DEPS}
  COMMENT "mips2c: translating decomp asm to C")

add_library(spyro_game_m2c OBJECT ${M2C_OUTS})
target_include_directories(spyro_game_m2c PRIVATE "${CMAKE_SOURCE_DIR}/src/game")
target_compile_options(spyro_game_m2c PRIVATE ${SPYRO_HOST_FLAGS} -w)

file(GLOB PORT_SRCS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/game/*.c")
add_library(spyro_game_port OBJECT ${PORT_SRCS})
target_include_directories(spyro_game_port PRIVATE
  "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/platform/common")
target_compile_options(spyro_game_port PRIVATE ${SPYRO_HOST_FLAGS})
