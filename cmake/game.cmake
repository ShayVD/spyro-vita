# The decomp's C (main executable part), built from the patched copy.
# Overlays: cmake/overlays.cmake. Generated data and translated asm: gen/.
set(SPYRO_HOST_FLAGS ${SPYRO_ARCH_FLAGS} ${SPYRO_OPT_FLAGS} -fno-pic -fno-stack-protector -fno-builtin
    ${SPYRO_GUARD_FLAGS})

file(GLOB S1_MAIN_SRCS "${S1_DIR}/src/*.c" "${S1_DIR}/src/gamestates/*.c")

add_library(spyro_game_main OBJECT ${S1_MAIN_SRCS})
target_include_directories(spyro_game_main BEFORE PRIVATE
  "${CMAKE_SOURCE_DIR}/src/compat" "${S1_DIR}/include")
target_compile_definitions(spyro_game_main PRIVATE
  MODERN_COMPILER NDEBUG NNOCD SPYRO_PORT)
# -fcommon: uninitialised globals in the decomp C are also defined in
# asm/data/*.s; GCC 2.7.2 made them common symbols, so do we.
target_compile_options(spyro_game_main PRIVATE ${SPYRO_HOST_FLAGS} -fcommon
  -include "${CMAKE_SOURCE_DIR}/src/compat/port_prefix.h")
target_link_libraries(spyro_game_main PRIVATE psycross)

# The decomp's main() becomes spyro_decomp_main(); src/game/port.c's
# spyro_main() sets up PS1 memory first.
set_source_files_properties("${S1_DIR}/src/main.c" PROPERTIES
  COMPILE_DEFINITIONS "main=spyro_decomp_main")

# Data the decomp keeps in asm/data/*.s, as host assembly with the same bytes
# and labels (tools/data2s.py, decisions D15). PsyQ's own data is PsyCross's.
set(S1_DATA_FILES
  asm/data/game.data.s asm/data/game.sdata.s asm/data/game.sbss.s
  asm/data/game.bss.s asm/data/math.data.s asm/data/titlescreen_data.s
  asm/data/save_header.s asm/data/sony_text.s asm/data/pad_act_align.s
  asm/data/unknown_data.s asm/data/entrance_stones.s
  asm/data/initialization.sdata.s
  # PsyQ sqrt table, read directly by collision.s and 42CC4.s
  asm/data/psyq.data.s:D_80074B84)
set(S1_DATA_DEPS)
foreach(f ${S1_DATA_FILES})
  string(REGEX REPLACE ":.*" "" f "${f}")
  list(APPEND S1_DATA_DEPS "${S1_DIR}/${f}")
endforeach()
# PS1 layout (decisions D23): the game's data labels sit at their psx.map
# addresses in the RAM arena, which needs the arena at 0x80000000 (linux32).
option(SPYRO_PSX_LAYOUT "Game data at its PS1 addresses in the arena" ON)
set(S1_DATA_LAYOUT)
set(S1_DATA_LAYOUT_DEPS)
if(SPYRO_PSX_LAYOUT)
  set(S1_DATA_LAYOUT --map "${SPYRO_MATCHING_DIR}/build/psx.map")
  set(S1_DATA_LAYOUT_DEPS "${SPYRO_MATCHING_DIR}/build/psx.map")
  add_compile_definitions(SPYRO_PSX_LAYOUT=1)
endif()
add_custom_command(OUTPUT "${GEN_DIR}/s1_data.S"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/data2s.py"
    --root "${S1_DIR}" --allow "${CMAKE_SOURCE_DIR}/tools/data2s_allow.txt"
    ${S1_DATA_LAYOUT} -o "${GEN_DIR}/s1_data.S" ${S1_DATA_FILES}
  DEPENDS ${S1_DATA_DEPS} "${CMAKE_SOURCE_DIR}/tools/data2s.py"
          "${CMAKE_SOURCE_DIR}/tools/data2s_allow.txt" ${S1_DATA_LAYOUT_DEPS}
  COMMENT "data2s: decomp asm/data -> host assembly")
set(S1_DATA_SRC "${GEN_DIR}/s1_data.S")
if(SPYRO_HOST STREQUAL "vita")
  include(cmake/vita_arena.cmake) # the arena is not at 0x80000000 (D25)
endif()
add_library(spyro_game_data OBJECT "${S1_DATA_SRC}")
target_compile_options(spyro_game_data PRIVATE ${SPYRO_ARCH_FLAGS})

include(cmake/overlays.cmake)
include(cmake/mips2c.cmake)

add_library(spyro_game STATIC $<TARGET_OBJECTS:spyro_game_main>
  $<TARGET_OBJECTS:spyro_game_data> $<TARGET_OBJECTS:spyro_game_ovl>
  $<TARGET_OBJECTS:spyro_game_m2c> $<TARGET_OBJECTS:spyro_game_port>)
