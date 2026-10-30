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
