# linux32 headless platform layer: soft GPU through PsyCross's OT walker,
# virtual vblanks and the pad script (docs/porting-plan.md §5).
add_executable(test_platform_linux32 test_platform_linux32.c)
target_link_libraries(test_platform_linux32 PRIVATE spyro_platform_linux32
  spyro_platform_common psycross m)
target_include_directories(test_platform_linux32 PRIVATE "${PSYCROSS_DIR}/src")
target_compile_options(test_platform_linux32 PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(test_platform_linux32 PRIVATE -m32 -no-pie)
add_test(NAME platform_linux32
  COMMAND test_platform_linux32 "${CMAKE_CURRENT_BINARY_DIR}")
