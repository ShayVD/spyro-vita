# The output ring's level control (vita-port-kit's vpk_audio_ring with psx_audio_ring.h's settings, D88, D107)
# against a model of the Vita's clocks.
add_executable(test_audio_ring test_audio_ring.c
  "${CMAKE_SOURCE_DIR}/third_party/vita-port-kit/vita/runtime/audioout/vpk_audio_ring.c")
target_include_directories(test_audio_ring PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/common")
target_compile_options(test_audio_ring PRIVATE ${SPYRO_ARCH_FLAGS} ${SPYRO_OPT_FLAGS} ${SPYRO_GUARD_FLAGS})
target_link_options(test_audio_ring PRIVATE -m32 -no-pie)
target_link_libraries(test_audio_ring PRIVATE m)
add_test(NAME audio_ring COMMAND test_audio_ring 10)
