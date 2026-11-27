# lp64/cmake's toolchains on a stand-in game (no game's code). linux64.toolchain.cmake: lp64_p32.h forced in, the game's main.c built with
# -Dmain=lp64_game_main, lp64_low_image.c linked, static and non-PIE, run through CMAKE_CROSSCOMPILING_EMULATOR by
# ctest. Run by lp64/test/run_tests.sh:
#   cmake -S lp64/cmake/test -B build/lp64-cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=lp64/cmake/linux64.toolchain.cmake
#   cmake --build build/lp64-cmake && ctest --test-dir build/lp64-cmake --output-on-failure
# linuxa64.toolchain.cmake (arm64 image) and macos.toolchain.cmake (a Mac): the offset model's runtime test instead.
# Written for the kit, after spyro-vita's linux64 build (CMakeLists.txt, src/platform/linux32/CMakeLists.txt @ c25a3ea).
cmake_minimum_required(VERSION 3.20)
project(lp64_cmake_test C)
enable_testing()
set(rt ${CMAKE_CURRENT_SOURCE_DIR}/../../runtime)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  # the offset model (linuxa64.toolchain.cmake, macos.toolchain.cmake): the runtime's own test, linked as the
  # toolchain links a game, run natively
  add_executable(game ${rt}/p32_image/test/p32_offset_test.c ${rt}/p32_image/lp64_p32_image.c)
  target_include_directories(game PRIVATE ${rt}/p32_image)
  target_compile_options(game PRIVATE -Wall -Wextra -Werror)
  add_test(NAME game COMMAND game)
  set_tests_properties(game PROPERTIES PASS_REGULAR_EXPRESSION "p32_offset_test: ok")
  return()
endif()
add_executable(game main.c ${rt}/low_image/lp64_low_image.c)
target_include_directories(game PRIVATE ${rt}/low_image)
set_source_files_properties(main.c PROPERTIES COMPILE_DEFINITIONS main=lp64_game_main)
target_compile_options(game PRIVATE -Wall -Wextra -Werror)
target_link_libraries(game PRIVATE pthread)
add_test(NAME game COMMAND game 3)
set_tests_properties(game PROPERTIES PASS_REGULAR_EXPRESSION "game: ok")
