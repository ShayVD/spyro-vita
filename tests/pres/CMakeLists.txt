# Stage 7e (decisions D125): the presenter's draw (src/game/pres/).
#  - pres_link: from GamestateDraw_pres, every name the presenter's object
#    reaches outside itself is its own (context, snapshot, renderers), read
#    only, its output (none since 7f, D126), or a game function that names no
#    writable data (link_check.py), so the presenter writes nothing of the
#    game's by name.
#  - pres_demo: the attract demo to vblank 5,000 with --interp --pres-check:
#    every in-between frame in play the same from the presenter as from
#    D30's pass, record for record.
if(NOT TARGET spyro_pres_decomp)
  return()
endif()
# Python by its path: ctest runs an imported executable target under the
# cross-compiling emulator (qemu-i386)
add_test(NAME pres_link
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/link_check.py" "${CMAKE_OBJDUMP}" "${CMAKE_NM}"
                 "${CMAKE_BINARY_DIR}/pres_draw.o" "$<TARGET_OBJECTS:spyro_game_port>"
                 "$<TARGET_OBJECTS:spyro_platform_common>" "$<TARGET_OBJECTS:spyro_game_main>")
if(TARGET spyro AND EXISTS "${CMAKE_SOURCE_DIR}/disc/files/manifest.json")
  add_test(NAME pres_demo
           COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/demo_check.sh" "$<TARGET_FILE:spyro>" "${CMAKE_SOURCE_DIR}/disc/files"
                   "${CMAKE_BINARY_DIR}/pres_demo")
endif()
