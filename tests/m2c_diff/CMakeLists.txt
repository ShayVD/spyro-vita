# Differential test: translated asm (tools/mips2c) against the original
# running in PCSX-Redux (docs/porting-plan.md §7.3, §9 step 13). First unit:
# math.s. The snapshots hold game RAM, so they live outside git:
#   tools/oracle/redux logs/m2c-math ORACLE_END=9000 ORACLE_TRACE_MAX=20 \
#     ORACLE_TRACE=$(tools/oracle/asm_entries.py third_party/spyro-1/asm/math.s --trace) \
#     ORACLE_GLOBALS=80076DD0:110,800785A8:30
# Without logs/m2c-math the test is skipped.
set(M2C_DIFF_ENTRIES "${GEN_DIR}/m2c_diff_math_entries.h")
add_custom_command(OUTPUT "${M2C_DIFF_ENTRIES}"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/oracle/asm_entries.py"
    "${S1_DIR}/asm/math.s" --header "${SPYRO_MATCHING_DIR}/build/psx.map"
    > "${M2C_DIFF_ENTRIES}"
  DEPENDS "${S1_DIR}/asm/math.s" "${CMAKE_SOURCE_DIR}/tools/oracle/asm_entries.py"
    "${SPYRO_MATCHING_DIR}/build/psx.map")
add_executable(test_m2c_diff test_m2c_diff.c "${M2C_DIFF_ENTRIES}")
target_include_directories(test_m2c_diff PRIVATE "${GEN_DIR}"
  "${CMAKE_SOURCE_DIR}/src/game" "${CMAKE_SOURCE_DIR}/src/platform/common")
target_compile_definitions(test_m2c_diff PRIVATE
  M2C_DIFF_ENTRIES="m2c_diff_math_entries.h")
target_link_libraries(test_m2c_diff PRIVATE spyro_game spyro_platform_linux32
  spyro_platform_common psycross m pthread)
target_compile_options(test_m2c_diff PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(test_m2c_diff PRIVATE -m32 -no-pie)
add_test(NAME m2c_diff_math COMMAND test_m2c_diff "${CMAKE_SOURCE_DIR}/logs/m2c-math")
set_tests_properties(m2c_diff_math PROPERTIES SKIP_RETURN_CODE 77)
