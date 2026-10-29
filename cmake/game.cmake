# The decomp's C (main executable part), built from the patched copy.
# Overlays: cmake/overlays.cmake. Generated data and translated asm: gen/.
set(SPYRO_HOST_FLAGS -m32 -O1 -g -fno-pic -fno-stack-protector -fno-builtin
    ${SPYRO_GUARD_FLAGS})

file(GLOB S1_MAIN_SRCS "${S1_DIR}/src/*.c" "${S1_DIR}/src/gamestates/*.c")

add_library(spyro_game_main OBJECT ${S1_MAIN_SRCS})
target_include_directories(spyro_game_main BEFORE PRIVATE
  "${CMAKE_SOURCE_DIR}/src/compat" "${S1_DIR}/include")
target_compile_definitions(spyro_game_main PRIVATE
  MODERN_COMPILER NDEBUG NNOCD SPYRO_PORT)
target_compile_options(spyro_game_main PRIVATE ${SPYRO_HOST_FLAGS}
  -include "${CMAKE_SOURCE_DIR}/src/compat/port_prefix.h")
target_link_libraries(spyro_game_main PRIVATE psycross)

# The decomp's main() becomes spyro_main(); ours sets up the arena first.
set_source_files_properties("${S1_DIR}/src/main.c" PROPERTIES
  COMPILE_DEFINITIONS "main=spyro_main")

# Data the decomp keeps in asm/data/*.s, as host assembly with the same bytes
# and labels (tools/data2s.py, decisions D15). PsyQ's own data is PsyCross's.
set(S1_DATA_FILES
  asm/data/game.data.s asm/data/game.sdata.s asm/data/game.sbss.s
  asm/data/game.bss.s asm/data/math.data.s asm/data/titlescreen_data.s
  asm/data/save_header.s asm/data/sony_text.s asm/data/pad_act_align.s
  asm/data/unknown_data.s asm/data/entrance_stones.s
  asm/data/initialization.sdata.s)
set(S1_DATA_DEPS)
foreach(f ${S1_DATA_FILES})
  list(APPEND S1_DATA_DEPS "${S1_DIR}/${f}")
endforeach()
add_custom_command(OUTPUT "${GEN_DIR}/s1_data.S"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/data2s.py"
    --root "${S1_DIR}" --allow "${CMAKE_SOURCE_DIR}/tools/data2s_allow.txt"
    -o "${GEN_DIR}/s1_data.S" ${S1_DATA_FILES}
  DEPENDS ${S1_DATA_DEPS} "${CMAKE_SOURCE_DIR}/tools/data2s.py"
          "${CMAKE_SOURCE_DIR}/tools/data2s_allow.txt"
  COMMENT "data2s: decomp asm/data -> host assembly")
add_library(spyro_game_data OBJECT "${GEN_DIR}/s1_data.S")
target_compile_options(spyro_game_data PRIVATE -m32)

add_library(spyro_game STATIC $<TARGET_OBJECTS:spyro_game_main>
  $<TARGET_OBJECTS:spyro_game_data>)
