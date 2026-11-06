# The walk's lane form of the lean vertex's rules (psx_lean.h, D105) against
# the rules, the lane operations in C; the mutant (qsub16 without its
# saturation) must fail. The ARMv6 SIMD instructions themselves are checked
# with the Vita's compiler under qemu-arm: tests/lean/arm_check.sh.
foreach(t test_lean test_lean_mutant)
  add_executable(${t} test_lean.c)
  target_include_directories(${t} PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/common")
  target_compile_options(${t} PRIVATE ${SPYRO_ARCH_FLAGS} ${SPYRO_OPT_FLAGS} ${SPYRO_GUARD_FLAGS})
  target_link_options(${t} PRIVATE -m32 -no-pie)
endforeach()
target_compile_definitions(test_lean_mutant PRIVATE LEAN_MUTANT=1)
add_test(NAME lean COMMAND test_lean 4194304)
add_test(NAME lean_mutant COMMAND test_lean_mutant 65536)
set_tests_properties(lean_mutant PROPERTIES WILL_FAIL TRUE)
