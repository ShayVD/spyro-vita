# The PC toolchains on a stand-in app (no game's code): the backend's own test (targets/pc/test/pc_test.c, the
# conformance run and the PC's checks) built with the platform interface and SDL3 the way a port builds its game. On
# macOS (pc-macos.toolchain.cmake): an app bundle from targets/pc/macos/Info.plist.in, signed ad hoc, SDL3 static, only
# system frameworks linked; on Linux (pc-linux-arm64.toolchain.cmake) a dynamic executable with lp64_low_image_dyn.c.
# The offset model's runtime test is built beside it with the same flags.
#   targets/pc/tools/sdl3_macos.sh
#   cmake -S targets/pc/cmake/test -B build/pc-cmake -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=targets/pc/cmake/pc-macos.toolchain.cmake -DENC_SDL3_PREFIX=$PWD/build/sdl3-macos
#   cmake --build build/pc-cmake && ctest --test-dir build/pc-cmake --output-on-failure
# Written for the kit, after spyro-vita src/platform/pc/CMakeLists.txt @ 36d9e07 (MIT; D265, D266).
cmake_minimum_required(VERSION 3.20)
project(enc_pc_cmake_test C)
enable_testing()
set(k ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3-static)
add_library(encore_pc STATIC ${k}/core/src/enc_common.c ${k}/core/src/enc_blit.c ${k}/core/src/enc_disc.c
  ${k}/core/src/enc_event.c ${k}/core/src/enc_discimage.c ${k}/core/src/enc_co.c ${k}/targets/pc/encore/enc_pc.c
  ${k}/core/test/conformance.c)
target_include_directories(encore_pc PUBLIC ${k}/core/include ${k}/core/test ${k}/targets/pc/encore)
target_link_libraries(encore_pc PUBLIC SDL3::SDL3-static)
add_executable(app MACOSX_BUNDLE ${k}/targets/pc/test/pc_test.c)
target_link_libraries(app PRIVATE encore_pc)
if(APPLE)
  set_target_properties(app PROPERTIES OUTPUT_NAME EncoreTest
    MACOSX_BUNDLE_INFO_PLIST "${k}/targets/pc/macos/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "EncoreTest" MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.shayvd.encore.test"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0" MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0")
  # the linker's own ad hoc signature is lost when the bundle is assembled: sign the bundle
  add_custom_command(TARGET app POST_BUILD COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:app>")
  set(app_exe "$<TARGET_FILE:app>")
else()
  target_sources(app PRIVATE ${k}/lp64/runtime/low_image/lp64_low_image_dyn.c)
  target_include_directories(app PRIVATE ${k}/lp64/runtime/low_image)
  target_link_libraries(app PRIVATE m pthread dl)
  set(app_exe "$<TARGET_FILE:app>")
endif()
set(env "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;ENC_PC_ROOT=${CMAKE_BINARY_DIR}/root")
add_test(NAME app COMMAND ${app_exe})
set_tests_properties(app PROPERTIES ENVIRONMENT "${env}" PASS_REGULAR_EXPRESSION "pc_test: video")
# the offset model's runtime test, built with the same toolchain
set(rt ${k}/lp64/runtime)
add_executable(offset ${rt}/p32_image/test/p32_offset_test.c ${rt}/p32_image/lp64_p32_image.c)
target_include_directories(offset PRIVATE ${rt}/p32_image ${rt}/low_image)
if(NOT APPLE)
  target_sources(offset PRIVATE ${rt}/low_image/lp64_low_image_dyn.c)
  target_compile_definitions(offset PRIVATE LP64_TEST_DYN=1)
endif()
add_test(NAME offset COMMAND offset)
set_tests_properties(offset PROPERTIES PASS_REGULAR_EXPRESSION "p32_offset_test: ok")
