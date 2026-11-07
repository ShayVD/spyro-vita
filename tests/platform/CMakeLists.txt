# linux32 headless platform layer: soft GPU through our OT walk (gpu_walk.c,
# stage 8d, D117), virtual vblanks and the pad script (docs/porting-plan.md §5).
# The GTE register file and the RTP hooks are ours (gte_regs.c, gte_ops.c;
# stage 8c, D116); our libgpu (D115) reaches them through vcache.c. The walk
# parses polygons with the records' converter (native_draw.c).
add_executable(test_platform_linux32 test_platform_linux32.c
  "${CMAKE_SOURCE_DIR}/src/game/gte_regs.c" "${CMAKE_SOURCE_DIR}/src/game/gte_ops.c"
  "${CMAKE_SOURCE_DIR}/src/game/native_draw.c")
target_link_libraries(test_platform_linux32 PRIVATE spyro_platform_linux32
  spyro_platform_common m)
target_include_directories(test_platform_linux32 PRIVATE
  "${CMAKE_SOURCE_DIR}/src/game")
target_compile_options(test_platform_linux32 PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(test_platform_linux32 PRIVATE -m32 -no-pie)
add_test(NAME platform_linux32
  COMMAND test_platform_linux32 "${CMAKE_CURRENT_BINARY_DIR}")
