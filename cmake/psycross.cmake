# PsyCross (MIT) as a static library, built from the patched copy.
# PS1-native primitive layout (docs/porting-plan.md §5): no extended prim
# pointers, no PGXP. Our own libcd and SPU replace LIBCD.C, LIBSPU.C and the
# OpenAL backend (§6), and our file-backed libmcrd replaces LIBMCRD.C.
# The platform layer (window, timing, pad, GR_* renderer)
# comes from src/platform/<host> unless SPYRO_PSYCROSS_PLATFORM is ON.
option(SPYRO_PSYCROSS_PLATFORM "Use PsyCross's own SDL2/GL platform layer" OFF)

file(GLOB PSYCROSS_SRCS
  "${PSYCROSS_DIR}/src/psx/*.C"
  "${PSYCROSS_DIR}/src/gte/*.cpp"
  "${PSYCROSS_DIR}/src/gpu/*.cpp")
list(FILTER PSYCROSS_SRCS EXCLUDE REGEX "/(LIBCD|LIBSPU|LIBMCRD)\\.C$")
if(SPYRO_PSYCROSS_PLATFORM)
  list(APPEND PSYCROSS_SRCS
    "${PSYCROSS_DIR}/src/PsyX_main.cpp"
    "${PSYCROSS_DIR}/src/pad/PsyX_pad.cpp"
    "${PSYCROSS_DIR}/src/render/PsyX_render.cpp"
    "${PSYCROSS_DIR}/src/render/glad.c")
endif()
set_source_files_properties(${PSYCROSS_SRCS} PROPERTIES LANGUAGE CXX)
set_source_files_properties("${PSYCROSS_DIR}/src/render/glad.c" PROPERTIES LANGUAGE C)

add_library(psycross STATIC ${PSYCROSS_SRCS})
target_include_directories(psycross PUBLIC
  "${PSYCROSS_DIR}/include" "${PSYCROSS_DIR}/include/psx")
target_include_directories(psycross PRIVATE "${PSYCROSS_DIR}/src")
target_compile_definitions(psycross PRIVATE $<$<NOT:$<BOOL:${SPYRO_PSYCROSS_PLATFORM}>>:PSYX_NO_SDL>)
target_compile_definitions(psycross PUBLIC
  USE_EXTENDED_PRIM_POINTERS=0 USE_PGXP=0)
target_compile_options(psycross PRIVATE ${SPYRO_GUARD_FLAGS} -w
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-narrowing>)
