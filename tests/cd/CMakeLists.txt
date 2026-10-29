# libcd over files and the minimal libspu (src/platform/common, docs/porting-plan.md §6).
# The CD cases need the extracted disc; they skip (exit 77) when it is missing.
add_executable(test_cd test_cd.c)
target_link_libraries(test_cd PRIVATE spyro_platform_common psycross)
target_compile_options(test_cd PRIVATE -m32 -O1 -g ${SPYRO_GUARD_FLAGS})
target_link_options(test_cd PRIVATE -m32 -no-pie)
add_test(NAME cd COMMAND test_cd "${CMAKE_SOURCE_DIR}/disc/files")
set_tests_properties(cd PROPERTIES SKIP_RETURN_CODE 77)
