# 32-bit Linux host debug build: headless, deterministic virtual vblanks,
# software renderer for PsyCross's GR_* layer, PNG screenshots.
file(GLOB LINUX32_SRCS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Platform layer without the game, so tests can link it too.
list(FILTER LINUX32_SRCS EXCLUDE REGEX "/main\\.c$")
add_library(spyro_platform_linux32 STATIC ${LINUX32_SRCS})
target_include_directories(spyro_platform_linux32 PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/platform/common")
target_include_directories(spyro_platform_linux32 PRIVATE "${PSYCROSS_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/game") # present.h only
target_link_libraries(spyro_platform_linux32 PUBLIC spyro_platform_common psycross)
target_compile_options(spyro_platform_linux32 PRIVATE ${SPYRO_HOST_FLAGS})

add_executable(spyro main.c)
target_link_libraries(spyro PRIVATE spyro_game spyro_platform_linux32
  spyro_platform_common psycross m pthread)
target_include_directories(spyro PRIVATE "${CMAKE_SOURCE_DIR}/src/game")
target_compile_options(spyro PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(spyro PRIVATE -m32 -no-pie)
