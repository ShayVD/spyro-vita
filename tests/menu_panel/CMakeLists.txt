# The port's options panel (src/platform/common/menu_panel.c): draws the
# Vita menu and writes it as menu_panel.ppm in the build tree to look at.
add_executable(test_menu_panel test_menu_panel.c)
target_link_libraries(test_menu_panel PRIVATE spyro_platform_common psycross)
target_compile_options(test_menu_panel PRIVATE -m32 -O1 -g ${SPYRO_GUARD_FLAGS})
target_link_options(test_menu_panel PRIVATE -m32 -no-pie)
add_test(NAME menu_panel COMMAND test_menu_panel "${CMAKE_CURRENT_BINARY_DIR}/menu_panel.ppm")
