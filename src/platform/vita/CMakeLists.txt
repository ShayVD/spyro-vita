# PS Vita build (decisions D4, D7, D25, D26): the same game, PsyCross and
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
set(VITA_VERSION "01.00")
# Extended memory for the app (D7), set as the cache value vita_create_vpk reads.
set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12" CACHE STRING "vita-mksfoex flags")

# PsyX_render.h only types TextureID for its GL renderers; the software
# rasterizer (D26) uses the same handle type.
target_compile_definitions(psycross PUBLIC RENDERER_OGL)
# cmake/psycross.cmake gives PsyCross no -O (the linux32 debug build runs it
# at -O0, and its pictures are the reference); on the Vita its GTE and OT
# walk are hot, so optimise it (no x87 excess precision on ARM).
target_compile_options(psycross PRIVATE ${SPYRO_OPT_FLAGS})

# The game's C reads its data labels through arena pointers (D25).
foreach(t spyro_game_main spyro_game_ovl spyro_game_m2c spyro_game_port)
  target_compile_options(${t} PRIVATE "SHELL:-include ${VITA_ARENA_SYMS_H}")
  add_dependencies(${t} spyro_vita_arena_syms)
endforeach()

file(GLOB VITA_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
# The software rasterizer and display-rect reader are linux32's (D26). The
# rasterizer's GR_* names get a soft_ prefix: gpu.c picks it or the GPU
# renderer (gpu_gl.c) at run time (D35).
set(LINUX32_DIR "${CMAKE_SOURCE_DIR}/src/platform/linux32")
set_source_files_properties("${LINUX32_DIR}/gpu_soft.c" PROPERTIES
  COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/gpu_soft_names.h")
add_executable(spyro_vita ${VITA_SRCS}
  "${LINUX32_DIR}/gpu_soft.c" "${LINUX32_DIR}/png.c")
target_include_directories(spyro_vita PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/src/platform/common" "${LINUX32_DIR}" "${PSYCROSS_DIR}/src")
target_compile_options(spyro_vita PRIVATE ${SPYRO_OPT_FLAGS} ${SPYRO_GUARD_FLAGS}
  -Wall -Wno-unused-function)
# Link list from duke-nukem-advance-tc-vita's Common.mak (D7): vitasdk's SDL2
# and the vitaGL stack for the GPU renderer (D35).
target_link_libraries(spyro_vita PRIVATE spyro_game spyro_platform_common psycross
  SDL2 vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub
  SceDisplay_stub SceCtrl_stub SceAudio_stub SceAudioIn_stub SceSysmodule_stub
  SceGxm_stub SceCommonDialog_stub SceAppMgr_stub SceAppUtil_stub SceTouch_stub
  SceHid_stub SceMotion_stub ScePower_stub SceProcessmgr_stub SceIme_stub
  SceKernelDmacMgr_stub SceIofilemgr_stub SceLibKernel_stub stdc++ pthread m)
target_link_options(spyro_vita PRIVATE -Wl,-q
  # vita-elf-create appends its SCE data (~3 KB) after the text segment, and
  # the data segment starts at the next 64 KiB boundary: keep 16 KiB free
  # there (vitasdk's linker script reads __sce_headroom), or a build whose
  # text ends just below a boundary fails "segment 1 overlaps" (D32).
  -Wl,--defsym=__sce_headroom=0x4000
  # prof_draw.c times the update and the renderers (the "draw" lines)
  -Wl,--wrap=GamestateUpdate,--wrap=func_800258F0,--wrap=func_8001F158
  -Wl,--wrap=func_8001F798,--wrap=func_800208FC,--wrap=func_80020F34
  -Wl,--wrap=func_80022A2C,--wrap=func_80023AC4,--wrap=func_80059F8C
  -Wl,--wrap=func_80059A48,--wrap=func_80058D64,--wrap=func_80058BA8
  -Wl,--wrap=func_800573C8,--wrap=func_80050BD0
  # prof.c times these (the "time" lines in spyro.log)
  -Wl,--wrap=DrawOTag,--wrap=DrawPrim,--wrap=GR_DrawTriangles
  -Wl,--wrap=GR_CopyVRAM,--wrap=GR_ClearVRAM,--wrap=GR_ReadVRAM
  # mixer.c: XA events go to the mixer thread, SPU RAM writes wait for it (D37)
  -Wl,--wrap=psx_xa_sector,--wrap=psx_xa_reset,--wrap=SpuWrite,--wrap=SpuInit
  # vitaGL must set GXM up as for a game, in Vita3K too (gpu_gl.c)
  -Wl,--wrap=sceAppMgrGetBudgetInfo)

# Bubble icon and LiveArea (decisions D55): original art drawn by
# tools/vita/make_livearea.py at build time, checked for the Vita's PNG rules
# (8-bit indexed, these sizes) before it's packed.
# The art in SPYRO_LIVEAREA_DIR is converted (D59): disc/livearea (gitignored,
# tools/vita/livearea_local.sh) when it's there, else assets/livearea, the
# official art committed at the owner's decision (not under MIT; see its
# README). With neither, make_livearea.py draws D55's original art.
if(EXISTS "${CMAKE_SOURCE_DIR}/disc/livearea/bg.png")
  set(la_default "${CMAKE_SOURCE_DIR}/disc/livearea")
else()
  set(la_default "${CMAKE_SOURCE_DIR}/assets/livearea")
endif()
set(SPYRO_LIVEAREA_DIR "${la_default}" CACHE PATH
  "LiveArea art (icon0.png, bg.png, startup.png)")
set(LA_DIR "${CMAKE_CURRENT_BINARY_DIR}/livearea")
set(LA_PNGS "${LA_DIR}/icon0.png" "${LA_DIR}/bg.png" "${LA_DIR}/startup.png")
set(LA_LOCAL "${SPYRO_LIVEAREA_DIR}/icon0.png" "${SPYRO_LIVEAREA_DIR}/bg.png"
  "${SPYRO_LIVEAREA_DIR}/startup.png")
set(LA_ARGS "${LA_DIR}")
set(LA_DEPS "")
set(la_have_local ON)
foreach(f ${LA_LOCAL})
  if(NOT EXISTS "${f}")
    set(la_have_local OFF)
  endif()
endforeach()
if(la_have_local)
  message(STATUS "LiveArea: art from ${SPYRO_LIVEAREA_DIR}")
  set(LA_ARGS --from "${SPYRO_LIVEAREA_DIR}" "${LA_DIR}")
  set(LA_DEPS ${LA_LOCAL})
endif()
add_custom_command(OUTPUT ${LA_PNGS}
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/vita/make_livearea.py" ${LA_ARGS}
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/vita/make_livearea.py" --check
    "${LA_DIR}/icon0.png" 128x128 "${LA_DIR}/bg.png" 840x500 "${LA_DIR}/startup.png" 280x158
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/vita/make_livearea.py" ${LA_DEPS}
  COMMENT "LiveArea art")
add_custom_target(spyro_vita_livearea DEPENDS ${LA_PNGS})
set(LA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents")
# The licences travel with the binary (MIT, BSD, Apache and the LGPL ask for
# it; THIRD_PARTY_NOTICES.md, decisions D56).
set(VPK_LICENSES "${CMAKE_SOURCE_DIR}/LICENSE" licenses/LICENSE
  "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" licenses/THIRD_PARTY_NOTICES.md)
file(GLOB license_texts CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/licenses/*.txt")
foreach(f ${license_texts})
  get_filename_component(n "${f}" NAME)
  list(APPEND VPK_LICENSES "${f}" "licenses/${n}")
endforeach()

vita_create_self(eboot.bin spyro_vita)
vita_create_vpk(spyro-vita.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION} NAME "${VITA_APP_NAME}"
  FILE "${LA_DIR}/icon0.png" sce_sys/icon0.png
       "${LA_DIR}/bg.png" sce_sys/livearea/contents/bg.png
       "${LA_DIR}/startup.png" sce_sys/livearea/contents/startup.png
       "${LA_SRC}/template.xml" sce_sys/livearea/contents/template.xml
       "${CMAKE_SOURCE_DIR}/README.md" README.md
       ${VPK_LICENSES})
add_dependencies(spyro-vita.vpk-vpk spyro_vita_livearea)
# The VPK at the top of the build tree too: build/vita/spyro-vita.vpk.
add_custom_command(TARGET spyro-vita.vpk-vpk POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/spyro-vita.vpk.out"
    "${CMAKE_BINARY_DIR}/spyro-vita.vpk")
