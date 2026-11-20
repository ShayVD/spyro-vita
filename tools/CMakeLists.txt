# Host tools built with the linux32 debug build (tools/tc): the game's own
# platform code under a command line.
#   build/linux32/tools/pbp_extract EBOOT.PBP OUT_DIR   the digital copy's files (D157)
add_executable(pbp_extract pbp_extract.c)
target_link_libraries(pbp_extract PRIVATE spyro_platform_common)
target_compile_options(pbp_extract PRIVATE -m32 -O1 -g ${SPYRO_GUARD_FLAGS})
target_link_options(pbp_extract PRIVATE -m32 -no-pie)
