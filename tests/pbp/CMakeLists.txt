# The PS1 Classic EBOOT.PBP reader (src/platform/common/pbp.c, D157): AES
# against FIPS 197, a PBP made by the test (a plain header, deflate and LZRC
# stored blocks), and every EBOOT.PBP in disc/psn/*/ (the owner's digital
# copies, gitignored) extracted and checked against the region table, and
# against disc/files byte for byte when that dump is there. Without a digital
# copy the extraction cases skip (exit 77) and the first two still run.
add_executable(test_pbp test_pbp.c)
target_link_libraries(test_pbp PRIVATE spyro_platform_common)
target_compile_options(test_pbp PRIVATE -m32 -O1 -g ${SPYRO_GUARD_FLAGS})
target_link_options(test_pbp PRIVATE -m32 -no-pie)
add_test(NAME pbp COMMAND test_pbp "${CMAKE_CURRENT_BINARY_DIR}/work" "${CMAKE_SOURCE_DIR}/disc/psn"
  "${CMAKE_SOURCE_DIR}/disc/files")
set_tests_properties(pbp PROPERTIES SKIP_RETURN_CODE 77)
