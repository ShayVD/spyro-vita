# 60 Hz logic, opt-in (decisions D130; docs/logic-60hz-audit.md §7.4,
# docs/tick60.md): the update path compiled a second time.
#
# The files that hold 60 Hz hunks (#if SPYRO_TICK60 ... #else <the
# original> #endif, in patches/spyro-1-port/ and in our hand C), and the
# callers between them and GamestateUpdate, are compiled again with
# -DSPYRO_TICK60=1 and -include src/game/tick60/tick60_rules.h, together
# with src/game/tick60/ (the rules' own C). The objects are linked into one
# relocatable object (tick60_update.o) in which tools/tick60_link.py makes
# every function local but the entry, GamestateUpdate renamed
# GamestateUpdate_60: inside it each call of a function the set defines
# goes to the set's own copy, every other name stays the game's. Unlike the
# presenter's (cmake/presenter.cmake), the 60 Hz rules write the game's own
# memory: their data names stay the game's (the tool refuses a global data
# definition in the set: on ARM the set's code would reach its own copy
# through a section anchor, D142), and it refuses writable data of the
# set's own that isn't listed in it.
#
# The 30 Hz objects are not touched: built without SPYRO_TICK60 the hunks'
# #else is the original's text (tests/tick60/cpp_check.py proves it,
# ctest tick60_cpp).
#
# Not with the moved arena (D25, D97), as the presenter.
set(SPYRO_TICK60 ON)
if(SPYRO_ARENA_MOVED)
  set(SPYRO_TICK60 OFF)
endif()
set(T60_GAME_SRCS)
if(NOT SPYRO_TICK60)
  return()
endif()
target_compile_definitions(spyro_game_port PRIVATE SPYRO_TICK60_RULES=1)

set(T60_DIR "${CMAKE_SOURCE_DIR}/src/game/tick60")
set(T60_RULES_H "${T60_DIR}/tick60_rules.h")
set(T60_DEFS SPYRO_TICK60=1)

# the decomp's files, as spyro_game_main compiles them
set(T60_DECOMP_SRCS "${S1_DIR}/src/gamestates/update.c" "${S1_DIR}/src/pete.c"
    "${S1_DIR}/src/camera.c" "${S1_DIR}/src/moby_helpers.c")
add_library(spyro_t60_decomp OBJECT ${T60_DECOMP_SRCS})
target_include_directories(spyro_t60_decomp BEFORE PRIVATE
  "${CMAKE_SOURCE_DIR}/src/compat" "${S1_DIR}/include" "${CMAKE_SOURCE_DIR}/src/game")
target_compile_definitions(spyro_t60_decomp PRIVATE MODERN_COMPILER NDEBUG NNOCD SPYRO_PORT
  ${T60_DEFS})
target_compile_options(spyro_t60_decomp PRIVATE ${SPYRO_HOST_FLAGS} -fcommon
  "SHELL:-include ${CMAKE_SOURCE_DIR}/src/compat/port_prefix.h" "SHELL:-include ${T60_RULES_H}")
target_link_libraries(spyro_t60_decomp PRIVATE psycross_headers)

# ours, as spyro_game_port compiles them, and the rules' own C
set(T60_PORT_SRCS
  "${CMAKE_SOURCE_DIR}/src/game/pete_hand.c" "${CMAKE_SOURCE_DIR}/src/game/pete_dispatch_hand.c"
  "${CMAKE_SOURCE_DIR}/src/game/pete_transition_hand.c"
  "${CMAKE_SOURCE_DIR}/src/game/pete_state_hand.c" "${CMAKE_SOURCE_DIR}/src/game/camera_hand.c"
  "${CMAKE_SOURCE_DIR}/src/game/moby_lists_hand.c" "${T60_DIR}/t60_rules.c" "${T60_DIR}/t60_classes.c")
add_library(spyro_t60_port OBJECT ${T60_PORT_SRCS})
# the 30 Hz build's definitions (the presenter's, the walk reference's), then ours
target_compile_definitions(spyro_t60_port PRIVATE
  "$<TARGET_PROPERTY:spyro_game_port,COMPILE_DEFINITIONS>" ${T60_DEFS})
target_include_directories(spyro_t60_port PRIVATE
  "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/platform/common")
target_include_directories(spyro_t60_port AFTER PRIVATE
  "${CMAKE_SOURCE_DIR}/src/compat" "${S1_DIR}/include")
target_link_libraries(spyro_t60_port PRIVATE psycross_headers)
target_compile_options(spyro_t60_port PRIVATE ${SPYRO_HOST_FLAGS} "SHELL:-include ${T60_RULES_H}")

# every level's moby classes (src/game/tick60/t60_levels.h), preprocessed
# and stripped as overlays.cmake does them, with SPYRO_TICK60. In the
# pilot's levels (Artisans and its levels: docs/tick60.md) every class
# judged for 60 Hz runs every vblank; in the others only Sparx does (D142),
# and the rest the original's rules every other vblank (t60_rules.c).
set(T60_OVL_SRCS)
set(T60_LEVELS)
foreach(row ${ovl_rows})
  string(REGEX MATCH "^([^|]+)\\|([^|]+)\\|(.*)$" _ "${row}")
  set(ovl "${CMAKE_MATCH_1}")
  set(file "${CMAKE_MATCH_2}")
  set(cls "${CMAKE_MATCH_3}")
  if(NOT file STREQUAL "moby_update.c")
    continue()
  endif()
  list(APPEND T60_LEVELS "${ovl}")
  set(out "${GEN_DIR}/ovl60/${ovl}/moby_update.c")
  add_custom_command(OUTPUT "${out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN_DIR}/ovl60/${ovl}"
    COMMAND "${Python3_EXECUTABLE}" "${OVL_TOOL}" preprocess
      --classes "${cls}" --out "${out}" "${S1_DIR}"
      -- "${CMAKE_C_COMPILER}" ${OVL_CPP_FLAGS} -DOVERLAY_NAME=${ovl} -DIS_${ovl}
         -DSPYRO_TICK60=1 -include ${T60_RULES_H}
         "${S1_DIR}/src/overlays/${file}"
    DEPENDS "${S1_DIR}/src/overlays/${file}" "${OVL_TOOL}" "${T60_RULES_H}"
    IMPLICIT_DEPENDS C "${S1_DIR}/src/overlays/${file}"
    COMMENT "tick60: overlay ${ovl}'s ${file}"
    VERBATIM)
  list(APPEND T60_OVL_SRCS "${out}")
endforeach()
# t60_levels.h must name the same levels, in the same order
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${T60_DIR}/t60_levels.h")
file(STRINGS "${T60_DIR}/t60_levels.h" t60_rows REGEX "^T60_LEVEL\\(")
set(t60_names)
foreach(r ${t60_rows})
  string(REGEX MATCH "^T60_LEVEL\\(([A-Z0-9_]+)," _ "${r}")
  list(APPEND t60_names "${CMAKE_MATCH_1}")
endforeach()
if(NOT t60_names STREQUAL T60_LEVELS)
  message(FATAL_ERROR "src/game/tick60/t60_levels.h lists ${t60_names}, the overlays with "
    "moby_update.c are ${T60_LEVELS}")
endif()
add_library(spyro_t60_ovl OBJECT ${T60_OVL_SRCS})
target_include_directories(spyro_t60_ovl PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/common")
target_compile_options(spyro_t60_ovl PRIVATE ${SPYRO_HOST_FLAGS} -fcommon -w)

if(SPYRO_HOST STREQUAL "linux32")
  set(T60_LD_FLAGS -m32)
endif()
set(T60_OBJ "${CMAKE_BINARY_DIR}/tick60_update.o")
add_custom_command(OUTPUT "${T60_OBJ}"
  COMMAND "${CMAKE_C_COMPILER}" ${T60_LD_FLAGS} -r -nostdlib -o "${T60_OBJ}.r"
    "$<JOIN:$<TARGET_OBJECTS:spyro_t60_decomp>,;>" "$<JOIN:$<TARGET_OBJECTS:spyro_t60_port>,;>"
    "$<JOIN:$<TARGET_OBJECTS:spyro_t60_ovl>,;>"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/tick60_link.py"
    --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --entry GamestateUpdate=GamestateUpdate_60
    --report "${T60_OBJ}.txt" "${T60_OBJ}.r" "${T60_OBJ}"
  DEPENDS spyro_t60_decomp spyro_t60_port spyro_t60_ovl $<TARGET_OBJECTS:spyro_t60_decomp>
          $<TARGET_OBJECTS:spyro_t60_port> $<TARGET_OBJECTS:spyro_t60_ovl>
          "${CMAKE_SOURCE_DIR}/tools/tick60_link.py"
  COMMAND_EXPAND_LISTS
  COMMENT "tick60: one object from the update path built again (tick60_update.o)")
set_source_files_properties("${T60_OBJ}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
set(T60_GAME_SRCS "${T60_OBJ}")
