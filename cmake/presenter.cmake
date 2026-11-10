# Stage 7e (decisions D125; docs/native-plan.md stage 7): the presenter's
# draw, the game's own draw code compiled a second time.
#
# The draw path's files (the decomp's draw.c, camera.c and environment.c,
# and our order_table.c and the hand C it calls) are compiled again with
# -include src/game/pres/pres_names.h, which renames every piece of state
# they write or must see blended onto the presenter's (the snapshot,
# pres_snap_names.h; the context, render_ctx_names.h), each renderer onto
# its 7d build (NAME_pres) and D30's hooks onto stubs. The objects are then
# linked into one relocatable object (pres_draw.o) in which every function
# is made local but the entry, GamestateDraw_pres: inside it each call of a
# function the set defines goes to the set's own copy, and every other name
# stays the game's. tests/pres/link_check.py checks what those are.
#
# Not with the moved arena (D25, D97: the labels through pointers): there
# interp_mode=2 falls back to D30's pass (present.c).
set(SPYRO_PRESENTER ON)
if(SPYRO_ARENA_MOVED)
  set(SPYRO_PRESENTER OFF)
endif()
set(PRES_DIR "${CMAKE_SOURCE_DIR}/src/game/pres")
# the snapshot, the presenter's context and its check (ordinary game code,
# in every build: without the presenter they only say it isn't there)
target_sources(spyro_game_port PRIVATE "${PRES_DIR}/pres_snap.c" "${PRES_DIR}/pres_draw.c"
  "${PRES_DIR}/pres_check.c")
set(PRES_GAME_SRCS)
if(NOT SPYRO_PRESENTER)
  return()
endif()
target_compile_definitions(spyro_game_port PRIVATE SPYRO_PRESENTER=1)

set(PRES_NAMES "${PRES_DIR}/pres_names.h")

# the decomp's files, as spyro_game_main compiles them
# (stage 7g, D128: spu.c for the flight results' sounds, completion.c for
# the inventory's percentage)
set(PRES_DECOMP_SRCS "${S1_DIR}/src/gamestates/draw.c" "${S1_DIR}/src/camera.c"
    "${S1_DIR}/src/environment.c" "${S1_DIR}/src/spu.c" "${S1_DIR}/src/completion.c")
add_library(spyro_pres_decomp OBJECT ${PRES_DECOMP_SRCS})
target_include_directories(spyro_pres_decomp BEFORE PRIVATE
  "${CMAKE_SOURCE_DIR}/src/compat" "${S1_DIR}/include" "${CMAKE_SOURCE_DIR}/src/game")
target_compile_definitions(spyro_pres_decomp PRIVATE MODERN_COMPILER NDEBUG NNOCD SPYRO_PORT)
target_compile_options(spyro_pres_decomp PRIVATE ${SPYRO_HOST_FLAGS} -fcommon
  "SHELL:-include ${CMAKE_SOURCE_DIR}/src/compat/port_prefix.h" "SHELL:-include ${PRES_NAMES}")
target_link_libraries(spyro_pres_decomp PRIVATE psycross_headers)

# ours, as spyro_game_port compiles them (moby_cull and sky_clip through
# wrappers that give their entries the presenter's plain form)
set(PRES_PORT_SRCS
  "${CMAKE_SOURCE_DIR}/src/game/order_table.c" "${CMAKE_SOURCE_DIR}/src/game/moby_lists_hand.c"
  "${CMAKE_SOURCE_DIR}/src/game/cyclorama_hand.c" "${CMAKE_SOURCE_DIR}/src/game/libgte.c"
  "${CMAKE_SOURCE_DIR}/src/game/math_hand.c" "${CMAKE_SOURCE_DIR}/src/game/images_hand.c"
  "${PRES_DIR}/moby_cull.c" "${PRES_DIR}/sky_clip.c"
  "${CMAKE_SOURCE_DIR}/src/game/credits_hand.c" "${PRES_DIR}/pres_ovl.c")
add_library(spyro_pres_port OBJECT ${PRES_PORT_SRCS})
target_compile_definitions(spyro_pres_port PRIVATE ${M2C_REPLAY_DEF} ${M2C_HAND_DEFS} ${M2C_PROF_DEF})
target_include_directories(spyro_pres_port PRIVATE
  "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/platform/common")
target_include_directories(spyro_pres_port AFTER PRIVATE
  "${CMAKE_SOURCE_DIR}/src/compat" "${S1_DIR}/include")
target_link_libraries(spyro_pres_port PRIVATE psycross_headers)
target_compile_options(spyro_pres_port PRIVATE ${SPYRO_HOST_FLAGS} "SHELL:-include ${PRES_NAMES}")
# the overlays' draws (stage 7g, D128), as spyro_game_ovl compiles them:
# the title's (TitlescreenDraw), each flight level's results (Flight5 and
# Flight4, called through D_8007567C and D_800758C4) and each balloon
# level's talk (Balloonist4, through D_800758D8): pres_ovl.c
set(PRES_OVL_SRCS "${GEN_DIR}/ovl/titlescreen_code/titlescreen.c")
foreach(lv 15 25 35 45 55)
  list(APPEND PRES_OVL_SRCS "${GEN_DIR}/ovl/LEVEL_${lv}/flight.c")
endforeach()
foreach(lv 10 11 13 20 21 22 30 40 50 60)
  list(APPEND PRES_OVL_SRCS "${GEN_DIR}/ovl/LEVEL_${lv}/balloonist.c")
endforeach()
add_library(spyro_pres_ovl OBJECT ${PRES_OVL_SRCS})
target_include_directories(spyro_pres_ovl PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/common"
  "${CMAKE_SOURCE_DIR}/src/game" "${PRES_DIR}")
target_compile_options(spyro_pres_ovl PRIVATE ${SPYRO_HOST_FLAGS} -fcommon -w
  "SHELL:-include ${PRES_NAMES}")
add_dependencies(spyro_pres_ovl spyro_game_ovl)
if(SPYRO_HOST STREQUAL "linux32")
  set(PRES_LD_FLAGS -m32)
endif()

# one relocatable object, the entry its only global definition. LoadImage
# is renamed here as well as in pres_names.h: libgpu.h #undefs it (a
# Windows guard), so the pause screens' calls in draw.c keep the game's
# name (stage 7g, D128)
set(PRES_OBJ "${CMAKE_BINARY_DIR}/pres_draw.o")
add_custom_command(OUTPUT "${PRES_OBJ}"
  COMMAND "${CMAKE_C_COMPILER}" ${PRES_LD_FLAGS} -r -nostdlib -o "${PRES_OBJ}.r"
    "$<JOIN:$<TARGET_OBJECTS:spyro_pres_decomp>,;>" "$<JOIN:$<TARGET_OBJECTS:spyro_pres_port>,;>"
    "$<JOIN:$<TARGET_OBJECTS:spyro_pres_ovl>,;>"
  COMMAND "${CMAKE_OBJCOPY}" --keep-global-symbol=GamestateDraw_pres
    --redefine-sym LoadImage=pres_LoadImage "${PRES_OBJ}.r" "${PRES_OBJ}"
  DEPENDS spyro_pres_decomp spyro_pres_port spyro_pres_ovl $<TARGET_OBJECTS:spyro_pres_decomp>
          $<TARGET_OBJECTS:spyro_pres_port> $<TARGET_OBJECTS:spyro_pres_ovl>
  COMMAND_EXPAND_LISTS
  COMMENT "presenter: one object from the draw path built again (pres_draw.o)")
set_source_files_properties("${PRES_OBJ}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

# the snapshot's copies at their labels' offsets in the mirror
set(PRES_SYMS_S "${GEN_DIR}/pres_snap_syms.S")
set(PRES_TABLE_C "${GEN_DIR}/pres_snap_table.c")
add_custom_command(OUTPUT "${PRES_SYMS_S}" "${PRES_TABLE_C}"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/pres_snap_syms.py"
    --map "${SPYRO_PSX_MAP}" --names "${PRES_DIR}/pres_snap_names.h" --names "${PRES_NAMES}"
    --asm "${PRES_SYMS_S}" --table "${PRES_TABLE_C}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/pres_snap_syms.py" "${PRES_DIR}/pres_snap_names.h" "${PRES_NAMES}" "${SPYRO_PSX_MAP}"
  COMMENT "presenter: the snapshot's labels (pres_snap_syms.S)")
add_library(spyro_pres_snap OBJECT "${PRES_SYMS_S}" "${PRES_TABLE_C}")
target_include_directories(spyro_pres_snap PRIVATE "${PRES_DIR}" "${CMAKE_SOURCE_DIR}/src/game")
target_compile_options(spyro_pres_snap PRIVATE ${SPYRO_ARCH_FLAGS} ${SPYRO_OPT_FLAGS})
set(PRES_GAME_SRCS $<TARGET_OBJECTS:spyro_pres_snap> "${PRES_OBJ}")
