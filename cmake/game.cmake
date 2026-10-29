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

add_library(spyro_game STATIC $<TARGET_OBJECTS:spyro_game_main>)
# The decomp's main() becomes spyro_main(); ours sets up the arena first.
set_source_files_properties("${S1_DIR}/src/main.c" PROPERTIES
  COMPILE_DEFINITIONS "main=spyro_main")
