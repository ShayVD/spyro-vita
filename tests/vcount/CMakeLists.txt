# The Vita's widened vblank counter (vita-port-kit's vpk_vcount.h, D90) under
# the clock's real-time arithmetic (src/platform/common/clock.c, linked as it
# is, D104), across the 16-bit counter's wraps; the
# mutant build (the raw counter, as before D90) must hang and fail.
foreach(t test_vcount test_vcount_mutant)
  add_executable(${t} test_vcount.c "${CMAKE_SOURCE_DIR}/src/platform/common/clock.c")
  target_include_directories(${t} PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/vita-port-kit/vita/runtime/vcount"
    "${CMAKE_SOURCE_DIR}/src/platform/common")
  target_compile_options(${t} PRIVATE ${SPYRO_ARCH_FLAGS} ${SPYRO_OPT_FLAGS} ${SPYRO_GUARD_FLAGS})
  target_link_options(${t} PRIVATE -m32 -no-pie)
endforeach()
target_compile_definitions(test_vcount_mutant PRIVATE VCOUNT_MUTANT=1)
add_test(NAME vcount COMMAND test_vcount)
add_test(NAME vcount_mutant COMMAND test_vcount_mutant)
set_tests_properties(vcount_mutant PROPERTIES WILL_FAIL TRUE)
