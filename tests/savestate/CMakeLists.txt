# Save states (decisions D51): every game global outside the arena is saved
# or known not to change (tools/savestate_globals.py).
add_test(NAME savestate_globals
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/savestate_globals.py"
    --nm "${CMAKE_NM}" --bin "$<TARGET_FILE:spyro>"
    "$<TARGET_OBJECTS:spyro_game_main>" "$<TARGET_OBJECTS:spyro_game_ovl>"
    "$<TARGET_OBJECTS:spyro_game_m2c>" "$<TARGET_OBJECTS:spyro_game_port>"
  COMMAND_EXPAND_LISTS)
