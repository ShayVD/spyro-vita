# The Vita's widened vblank counter (src/platform/vita/vcount.h, D89) under
# PsyX_WaitForTimestep's arithmetic, across the 16-bit counter's wraps; the
# mutant build (the raw counter, as before D89) must hang and fail.
foreach(t test_vcount test_vcount_mutant)
  add_executable(${t} test_vcount.c)
  target_include_directories(${t} PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/vita")
  target_compile_options(${t} PRIVATE ${SPYRO_ARCH_FLAGS} ${SPYRO_OPT_FLAGS} ${SPYRO_GUARD_FLAGS})
  target_link_options(${t} PRIVATE -m32 -no-pie)
endforeach()
target_compile_definitions(test_vcount_mutant PRIVATE VCOUNT_MUTANT=1)
add_test(NAME vcount COMMAND test_vcount)
add_test(NAME vcount_mutant COMMAND test_vcount_mutant)
set_tests_properties(vcount_mutant PROPERTIES WILL_FAIL TRUE)
