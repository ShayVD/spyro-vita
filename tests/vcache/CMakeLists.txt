# The vertex cache's generations' check (#218, vcache.c gc_check, host
# builds only): a renderer that changes an NgteProj field after a projection
# without ngte_proj_regen must be counted (gen_stale), under the renderer
# context that returns next; a new generation, or none, must not; and the
# check is off unless asked for.
# The cache alone (vcache.c from the common library) with the GTE register
# file; the test gives the platform's log.
add_executable(test_vcache_gen test_vcache_gen.c
  "${CMAKE_SOURCE_DIR}/src/game/gte_regs.c" "${CMAKE_SOURCE_DIR}/src/game/gte_ops.c")
target_link_libraries(test_vcache_gen PRIVATE spyro_platform_common m pthread)
target_include_directories(test_vcache_gen PRIVATE
  "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/platform/common")
target_compile_options(test_vcache_gen PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(test_vcache_gen PRIVATE -m32 -no-pie)
add_test(NAME vcache_gen COMMAND test_vcache_gen)

# The core-2 helper's claim protocol (#282, docs/vcache-core2.md): a model
# of vcache.c's table, store and lz_need with three threads (game, walk,
# helper) over frames shaped like Artisans' at 60 Hz, many times; every
# projection worked out exactly once and every answer the single thread's.
# No game code: it links nothing of the game's. The mutants (one deliberate
# error each in the protocol) must fail.
add_executable(test_vcache_helper test_vcache_helper.c)
target_link_libraries(test_vcache_helper PRIVATE pthread)
target_compile_options(test_vcache_helper PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(test_vcache_helper PRIVATE -m32 -no-pie)
add_test(NAME vcache_helper_race COMMAND test_vcache_helper --iters 60)
add_test(NAME vcache_helper_race_stress COMMAND test_vcache_helper --iters 30 --stress --seed 2)
foreach(m 1 2 3)
  add_test(NAME vcache_helper_mutant${m} COMMAND test_vcache_helper --iters 6 --seed ${m})
  set_tests_properties(vcache_helper_mutant${m} PROPERTIES ENVIRONMENT VC_RACE_MUTANT=${m})
  spyro_mutant_test(vcache_helper_mutant${m} "vcache helper race: [0-9]+ worked out twice, [0-9]+ never, [0-9]+ answers different, [0-9]+ reads different.*test_vcache_helper: FAILED")
endforeach()

# ... and under ThreadSanitizer where the toolchain has it (not i686's: TSan
# needs a 64-bit target; docs/vcache-core2.md gives the native macOS run)
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS -fsanitize=thread)
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=thread)
check_c_source_compiles("int main(void) { return 0; }" SPYRO_HAVE_TSAN)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(SPYRO_HAVE_TSAN)
  add_executable(test_vcache_helper_tsan test_vcache_helper.c)
  target_link_libraries(test_vcache_helper_tsan PRIVATE pthread)
  target_compile_options(test_vcache_helper_tsan PRIVATE -O1 -g -fsanitize=thread)
  target_link_options(test_vcache_helper_tsan PRIVATE -fsanitize=thread)
  add_test(NAME vcache_helper_race_tsan COMMAND test_vcache_helper_tsan --iters 20)
  set_tests_properties(vcache_helper_race_tsan PROPERTIES FAIL_REGULAR_EXPRESSION "ThreadSanitizer")
else()
  message(STATUS "test_vcache_helper: no ThreadSanitizer in this toolchain, its TSan run skipped")
endif()
