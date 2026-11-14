# The PS1 Classic EBOOT.PBP reader (src/platform/common/pbp.c, D157).
#   pbp       AES against FIPS 197; the region matchers; PBPs the test makes (a
#             plain header; stored, deflate and LZRC blocks, the LZRC ones from
#             the test's own encoder; hostile headers, tables and streams); and
#             every EBOOT.PBP in disc/psn/*/ (the owner's digital copies,
#             gitignored): extracted, checked against the region table and
#             against disc/files byte for byte when that dump is there, then
#             read in place through libcd and the native reads. Without a
#             digital copy the extraction cases skip (exit 77) after the made
#             ones ran: a failure in those still fails.
#   pbp_made  the same on build/linux32/pbp-made/*/EBOOT.PBP, which tests/pbp/make_pbp.py
#             makes from your own dump (deflate blocks, a plain header: the
#             reader in the game's place without a Store copy). Skips without.
# The PGD container's decryption and Sony's own LZRC streams run only in pbp,
# and only with a copy in disc/psn.
add_executable(test_pbp test_pbp.c)
target_link_libraries(test_pbp PRIVATE spyro_platform_common)
target_compile_options(test_pbp PRIVATE -m32 -O1 -g ${SPYRO_GUARD_FLAGS})
target_link_options(test_pbp PRIVATE -m32 -no-pie)
add_test(NAME pbp COMMAND test_pbp "${CMAKE_CURRENT_BINARY_DIR}/work" "${CMAKE_SOURCE_DIR}/disc/psn"
  "${CMAKE_SOURCE_DIR}/disc/files")
set_tests_properties(pbp PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME pbp_made COMMAND test_pbp "${CMAKE_CURRENT_BINARY_DIR}/work-made" "${CMAKE_BINARY_DIR}/pbp-made"
  "${CMAKE_SOURCE_DIR}/disc/files")
set_tests_properties(pbp_made PROPERTIES SKIP_RETURN_CODE 77)
