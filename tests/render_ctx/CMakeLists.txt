# Stage 7d (decisions D123): the presenter builds of the renderers
# (src/game/NAME_hand_pres.c, render_ctx.h).
#  - render_ctx_link: every symbol a presenter object needs from outside is
#    the presenter's own or one it may only read (link_check.sh's list), so
#    a presenter call writes nothing of the game's by name.
#  - render_ctx_demo: the attract demo's first 4,000 vblanks with
#    --render-ctx-check: every renderer's calls with 0 differences and 0
#    writes to the game's memory (the arena build, whose arena the check can
#    make read-only).
if(NOT TARGET spyro_game_port)
  return()
endif()
add_test(NAME render_ctx_link
         COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/link_check.sh" "${CMAKE_NM}" "$<TARGET_OBJECTS:spyro_game_port>")
if(TARGET spyro AND NOT SPYRO_NATIVE_MEM AND EXISTS "${CMAKE_SOURCE_DIR}/disc/files/manifest.json")
  add_test(NAME render_ctx_demo
           COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/demo_check.sh" "$<TARGET_FILE:spyro>" "${CMAKE_SOURCE_DIR}/disc/files"
                   "${CMAKE_BINARY_DIR}/render_ctx_demo")
endif()
