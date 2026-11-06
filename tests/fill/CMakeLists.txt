# ClearImage's row-by-row fill (psx_fill.h, D111) against the plain loop, the
# C path; the mutant (the wrapped piece one pixel short) must fail. The NEON
# path is checked with the Vita's compiler under qemu-arm:
# tests/fill/arm_check.sh.
foreach(t test_fill test_fill_mutant)
  add_executable(${t} test_fill.c)
  target_include_directories(${t} PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/common")
  target_compile_options(${t} PRIVATE ${SPYRO_ARCH_FLAGS} ${SPYRO_OPT_FLAGS} ${SPYRO_GUARD_FLAGS})
  target_link_options(${t} PRIVATE -m32 -no-pie)
endforeach()
target_compile_definitions(test_fill_mutant PRIVATE PSX_FILL_MUTANT=1)
add_test(NAME fill COMMAND test_fill 32768)
add_test(NAME fill_mutant COMMAND test_fill_mutant 4096)
set_tests_properties(fill_mutant PROPERTIES WILL_FAIL TRUE)
