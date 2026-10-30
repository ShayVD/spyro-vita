# Vita: the arena's address is only known at run time, so the game's data
# labels become pointers set at start-up (tools/vita/arena_syms.py, decisions
# D24). Swaps S1_DATA_SRC for the rewritten data; src/platform/vita
# force-includes the header into the game's C.
if(NOT SPYRO_PSX_LAYOUT)
  message(FATAL_ERROR "the Vita build needs SPYRO_PSX_LAYOUT (decisions D24)")
endif()
set(VITA_ARENA_SYMS_H "${GEN_DIR}/vita/arena_syms.h")
add_custom_command(OUTPUT "${GEN_DIR}/vita/s1_data_vita.S" "${VITA_ARENA_SYMS_H}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN_DIR}/vita"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/vita/arena_syms.py"
    "${GEN_DIR}/s1_data.S" "${GEN_DIR}/vita/s1_data_vita.S" "${VITA_ARENA_SYMS_H}"
  DEPENDS "${GEN_DIR}/s1_data.S" "${CMAKE_SOURCE_DIR}/tools/vita/arena_syms.py"
  COMMENT "arena_syms: data labels as arena pointers")
add_custom_target(spyro_vita_arena_syms DEPENDS "${VITA_ARENA_SYMS_H}")
set(S1_DATA_SRC "${GEN_DIR}/vita/s1_data_vita.S")
