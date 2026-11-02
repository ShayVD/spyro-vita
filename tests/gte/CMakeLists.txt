# GTE model against PCSX-Redux vectors (docs/porting-plan.md §9 step 8).
# The vectors come from the game's own GTE commands, so they live outside git:
#   tools/oracle/gte_sites.py disc/files/SCUS_942.28 logs/gte/sites.txt
#   tools/oracle/redux logs/gte ORACLE_END=9000 ORACLE_GTE=$PWD/logs/gte/sites.txt
# Without logs/gte/gte.bin the test is skipped.
add_executable(test_gte test_gte.c)
target_link_libraries(test_gte PRIVATE psycross m)
target_compile_options(test_gte PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(test_gte PRIVATE -m32 -no-pie)
add_test(NAME gte COMMAND test_gte "${CMAKE_SOURCE_DIR}/logs/gte/gte.bin")
set_tests_properties(gte PROPERTIES SKIP_RETURN_CODE 77)

# The inline GTE commands (src/game/gte_inline.h, fps plan C5, D72) against
# PsyCross's GTE_operator on random states, and against the same vectors.
add_executable(test_gte_inline test_gte_inline.c "${CMAKE_SOURCE_DIR}/src/game/gte_ops.c")
target_include_directories(test_gte_inline PRIVATE "${CMAKE_SOURCE_DIR}/src/game")
target_link_libraries(test_gte_inline PRIVATE psycross m)
target_compile_options(test_gte_inline PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(test_gte_inline PRIVATE -m32 -no-pie)
add_test(NAME gte_inline COMMAND test_gte_inline "${CMAKE_SOURCE_DIR}/logs/gte/gte.bin")

# The native GTE maths (src/game/gte_native.h, fast_render=2, D80) against
# gte_inline.h on random states and against the same vectors; the mutant
# build (one deliberate error) must fail.
add_executable(test_gte_native test_gte_native.c "${CMAKE_SOURCE_DIR}/src/game/gte_ops.c")
target_include_directories(test_gte_native PRIVATE "${CMAKE_SOURCE_DIR}/src/game")
target_link_libraries(test_gte_native PRIVATE psycross m)
target_compile_options(test_gte_native PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(test_gte_native PRIVATE -m32 -no-pie)
add_test(NAME gte_native COMMAND test_gte_native "${CMAKE_SOURCE_DIR}/logs/gte/gte.bin")
add_executable(test_gte_native_mutant test_gte_native.c "${CMAKE_SOURCE_DIR}/src/game/gte_ops.c")
target_include_directories(test_gte_native_mutant PRIVATE "${CMAKE_SOURCE_DIR}/src/game")
target_compile_definitions(test_gte_native_mutant PRIVATE NGTE_MUTATE=1)
target_link_libraries(test_gte_native_mutant PRIVATE psycross m)
target_compile_options(test_gte_native_mutant PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(test_gte_native_mutant PRIVATE -m32 -no-pie)
add_test(NAME gte_native_mutant COMMAND test_gte_native_mutant)
set_tests_properties(gte_native_mutant PROPERTIES WILL_FAIL TRUE)
