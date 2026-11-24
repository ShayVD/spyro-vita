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
