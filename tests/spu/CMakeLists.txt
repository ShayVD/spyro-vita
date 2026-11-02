# The native SPU mix (src/platform/common/spu_native.c, D84) against
# libspu.c's psx_spu_mix on random RAM, voices and XA; the mutant build (one
# rounding off in the C path) must fail. The NEON path is checked where it
# runs: tests/spu/neon_check.sh (an arm64 Mac), and Vita3K's WAV.
set(SPU_TEST_SRCS test_spu_native.c
  "${CMAKE_SOURCE_DIR}/src/platform/common/libspu.c"
  "${CMAKE_SOURCE_DIR}/src/platform/common/spu_native.c"
  "${CMAKE_SOURCE_DIR}/src/platform/common/xa.c")
foreach(t test_spu_native test_spu_native_mutant)
  add_executable(${t} ${SPU_TEST_SRCS})
  target_include_directories(${t} PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/common"
    "${PSYCROSS_DIR}/include" "${PSYCROSS_DIR}/include/psx")
  target_compile_options(${t} PRIVATE ${SPYRO_ARCH_FLAGS} ${SPYRO_OPT_FLAGS} ${SPYRO_GUARD_FLAGS})
  target_link_options(${t} PRIVATE -m32 -no-pie)
endforeach()
target_compile_definitions(test_spu_native_mutant PRIVATE SPU_NATIVE_MUTANT=1)
add_test(NAME spu_native COMMAND test_spu_native 20000)
add_test(NAME spu_native_mutant COMMAND test_spu_native_mutant 2000)
set_tests_properties(spu_native_mutant PROPERTIES WILL_FAIL TRUE)
