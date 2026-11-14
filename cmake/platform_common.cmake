# Platform code shared by all hosts (src/platform/common): arena, libcd over
# files, software SPU, XA. No OS window/GPU headers here.
file(GLOB PLATFORM_COMMON_SRCS CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/platform/common/*.c")
# vita-port-kit's portable modules (third_party/vita-port-kit, vendored: D107, D108, D110)
list(APPEND PLATFORM_COMMON_SRCS
  "${CMAKE_SOURCE_DIR}/third_party/vita-port-kit/vita/runtime/audioout/vpk_audio_ring.c"
  "${CMAKE_SOURCE_DIR}/third_party/vita-port-kit/vita/runtime/discimage/vpk_discimage.c"
  "${CMAKE_SOURCE_DIR}/third_party/vita-port-kit/vita/runtime/modmenu/vpk_menu.c"
  "${CMAKE_SOURCE_DIR}/third_party/vita-port-kit/vita/runtime/modmenu/vpk_menu_panel.c"
  # tiny-AES-c (kokke's upstream, vendored, the Unlicense) for the PS1 Classics
  # EBOOT's PGD header (pbp.c, D157): AES-128 (its header's own setting), the
  # ECB block functions only. aes128.c, the one file that includes its header,
  # gets the same switches.
  "${CMAKE_SOURCE_DIR}/third_party/tiny-AES-c/aes.c")
set_source_files_properties("${CMAKE_SOURCE_DIR}/third_party/tiny-AES-c/aes.c"
  "${CMAKE_SOURCE_DIR}/src/platform/common/aes128.c" PROPERTIES
  COMPILE_DEFINITIONS "ECB=1;CBC=0;CTR=0")
# The digital copy's block decoder runs on the Vita's CPU for every sector the
# game reads (D158): the fastest code the compiler gives, on every host.
set_source_files_properties("${CMAKE_SOURCE_DIR}/src/platform/common/pbp.c" PROPERTIES
  COMPILE_OPTIONS "-O3")
add_library(spyro_platform_common STATIC ${PLATFORM_COMMON_SRCS})
target_include_directories(spyro_platform_common PUBLIC
  "${CMAKE_SOURCE_DIR}/src/platform/common")
target_link_libraries(spyro_platform_common PUBLIC psycross_headers)
target_compile_options(spyro_platform_common PRIVATE
  ${SPYRO_ARCH_FLAGS} ${SPYRO_OPT_FLAGS} -fno-pic -fno-stack-protector ${SPYRO_GUARD_FLAGS})
