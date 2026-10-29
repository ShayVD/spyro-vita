# Platform code shared by all hosts (src/platform/common): arena, libcd over
# files, software SPU, XA. No OS window/GPU headers here.
file(GLOB PLATFORM_COMMON_SRCS CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/platform/common/*.c")
add_library(spyro_platform_common STATIC ${PLATFORM_COMMON_SRCS})
target_include_directories(spyro_platform_common PUBLIC
  "${CMAKE_SOURCE_DIR}/src/platform/common")
target_link_libraries(spyro_platform_common PUBLIC psycross)
target_compile_options(spyro_platform_common PRIVATE
  -m32 -O1 -g -fno-pic -fno-stack-protector ${SPYRO_GUARD_FLAGS})
