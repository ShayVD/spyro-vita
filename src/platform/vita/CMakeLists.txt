# PS Vita build (decisions D4, D7, D24, D25): the same game, PsyCross and
# src/platform/common as linux32, plus the Vita platform layer here; packed
# as a VPK.
#   cmake -S . -B build/vita -G Ninja -DSPYRO_HOST=vita \
#       -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
#   cmake --build build/vita      -> build/vita/spyro-vita.vpk
if(NOT DEFINED ENV{VITASDK} AND NOT VITASDK)
  set(VITASDK /usr/local/vitasdk)
elseif(NOT VITASDK)
  set(VITASDK $ENV{VITASDK})
endif()
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Spyro Vita")
set(VITA_TITLEID "SPYV00001")
set(VITA_VERSION "00.10")
# Extended memory for the app (D7), set as the cache value vita_create_vpk reads.
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12" CACHE STRING "vita-mksfoex flags")

# PsyX_render.h only types TextureID for its GL renderers; the software
# rasterizer (D25) uses the same handle type.
target_compile_definitions(psycross PUBLIC RENDERER_OGL)

# The game's C reads its data labels through arena pointers (D24).
foreach(t spyro_game_main spyro_game_ovl spyro_game_m2c spyro_game_port)
  target_compile_options(${t} PRIVATE "SHELL:-include ${VITA_ARENA_SYMS_H}")
  add_dependencies(${t} spyro_vita_arena_syms)
endforeach()

file(GLOB VITA_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
# The software rasterizer and display-rect reader are linux32's (D25).
set(LINUX32_DIR "${CMAKE_SOURCE_DIR}/src/platform/linux32")
add_executable(spyro_vita ${VITA_SRCS}
  "${LINUX32_DIR}/gpu_soft.c" "${LINUX32_DIR}/png.c")
target_include_directories(spyro_vita PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/src/platform/common" "${LINUX32_DIR}" "${PSYCROSS_DIR}/src")
target_compile_options(spyro_vita PRIVATE ${SPYRO_OPT_FLAGS} ${SPYRO_GUARD_FLAGS}
  -Wall -Wno-unused-function)
# Link list from duke-nukem-advance-tc-vita's Common.mak (D7): vitasdk's SDL2
# and the vitaGL stack for the GPU renderer to come.
target_link_libraries(spyro_vita PRIVATE spyro_game spyro_platform_common psycross
  SDL2 vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub
  SceDisplay_stub SceCtrl_stub SceAudio_stub SceAudioIn_stub SceSysmodule_stub
  SceGxm_stub SceCommonDialog_stub SceAppMgr_stub SceAppUtil_stub SceTouch_stub
  SceHid_stub SceMotion_stub ScePower_stub SceProcessmgr_stub SceIme_stub
  SceKernelDmacMgr_stub SceIofilemgr_stub SceLibKernel_stub stdc++ pthread m)
target_link_options(spyro_vita PRIVATE -Wl,-q)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/icon0.png"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/vita/make_icon.py"
    "${CMAKE_CURRENT_BINARY_DIR}/icon0.png"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/vita/make_icon.py")

vita_create_self(eboot.bin spyro_vita)
vita_create_vpk(spyro-vita.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION} NAME "${VITA_APP_NAME}"
  FILE "${CMAKE_CURRENT_BINARY_DIR}/icon0.png" sce_sys/icon0.png)
# The VPK at the top of the build tree too: build/vita/spyro-vita.vpk.
add_custom_command(TARGET spyro-vita.vpk-vpk POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/spyro-vita.vpk.out"
    "${CMAKE_BINARY_DIR}/spyro-vita.vpk")
