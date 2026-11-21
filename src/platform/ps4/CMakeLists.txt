# PS4 build (docs/ps4-plan.md P3-P4, decisions D193, D194): the same game
# and src/platform/common as the other hosts, this platform layer (the
# GLES2 renderer on the player's Piglet among it), and linux32's software
# rasterizer, PNG writer and log. Packed as a fake PKG. libScePigletv2VSH is
# OpenOrbis's stub: its imports bind to the copy gpu_gles.c loads.
#   eval "$(tools/ps4/sdk.sh --env)"
#   cmake -S . -B build/ps4 -G Ninja -DSPYRO_HOST=ps4 \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/ps4.toolchain.cmake
#   cmake --build build/ps4   -> build/ps4/pkg/IV0000-SPYT00002_00-SPYROPS4GAME0000.pkg
# CMake builds static libraries; the eboot's link is ld.lld with OpenOrbis's
# link.x, as its samples and the P2 spike link (D180): a PIE the PS4 loads at
# 0x400000.
set(LINUX32_DIR "${CMAKE_SOURCE_DIR}/src/platform/linux32")
file(GLOB PS4_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
# The software rasterizer's GR_* names get a soft_ prefix (the Vita's
# gpu_soft_names.h, D35): gr_ps4.c calls it and then the GLES2 renderer
# (gpu_gles.c), D194.
set_source_files_properties("${LINUX32_DIR}/gpu_soft.c" PROPERTIES
  COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/src/platform/vita/gpu_soft_names.h")
# The GLES2 renderer's pre-compiled shaders (D194, the owner's option C):
# the binaries a run with the player's Piglet pair saved (shader_dump=1),
# fetched from the PS4 into this folder, become a C table; on a PS4 without
# the pair the firmware's own Piglet loads them with glShaderBinary. Local
# and gitignored (disc/) until the owner decides how they ship (P5).
set(SPYRO_PS4_SHADERS "${CMAKE_SOURCE_DIR}/disc/ps4-shaders" CACHE PATH
  "The PS4 renderer's pre-compiled shader binaries (NAME-vs|fs-HASH.bin)")
file(GLOB PS4_SHADER_BINS CONFIGURE_DEPENDS "${SPYRO_PS4_SHADERS}/*.bin")
set(PS4_SHADERS_C "${CMAKE_CURRENT_BINARY_DIR}/ps4_shaders.c")
add_custom_command(OUTPUT "${PS4_SHADERS_C}"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/ps4/shaders_c.py"
    "${SPYRO_PS4_SHADERS}" "${PS4_SHADERS_C}"
  DEPENDS "${CMAKE_SOURCE_DIR}/tools/ps4/shaders_c.py" ${PS4_SHADER_BINS}
  COMMENT "PS4: the pre-compiled shaders (${SPYRO_PS4_SHADERS})"
  VERBATIM)
add_library(spyro_platform_ps4 STATIC ${PS4_SRCS} "${PS4_SHADERS_C}"
  "${LINUX32_DIR}/gpu_soft.c" "${LINUX32_DIR}/png.c" "${LINUX32_DIR}/log.c")
target_include_directories(spyro_platform_ps4 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${LINUX32_DIR}" "${CMAKE_SOURCE_DIR}/src/platform/common" "${CMAKE_SOURCE_DIR}/src/game")
target_link_libraries(spyro_platform_ps4 PRIVATE psycross_headers)
target_compile_options(spyro_platform_ps4 PRIVATE ${SPYRO_HOST_FLAGS} -Wall -Wno-unused-function)

set(PS4_ELF "${CMAKE_BINARY_DIR}/spyro.elf")
set(OO "${OO_PS4_TOOLCHAIN}")
# The renderers' hand C is reached only through linux32's wrappers, so its
# entries are pulled in by name, as src/platform/linux32/CMakeLists.txt does
# for the native build.
set(PS4_UNDEF)
foreach(f func_80050BD0 func_800258F0 func_8001F158 func_8001F798 func_800208FC func_80020F34
          func_80022A2C func_80023AC4 func_80059F8C func_80059A48 func_80058D64 func_8004EBA8)
  list(APPEND PS4_UNDEF --undefined=${f})
endforeach()
# __bss_start, _end and etext from --defsym: lld doesn't define them under
# link.x with -pie, and weak references become imports create-fself rejects
# (D180).
add_custom_command(OUTPUT "${PS4_ELF}"
  COMMAND "${PS4_LD}" -m elf_x86_64 -pie --script "${OO}/link.x" --eh-frame-hdr --build-id=none
    "--defsym=__bss_start=ADDR(.bss)" "--defsym=_end=ADDR(.bss)+SIZEOF(.bss)"
    "--defsym=etext=ADDR(.text)+SIZEOF(.text)"
    ${PS4_UNDEF} "-L${OO}/lib" "${OO}/lib/crt1.o"
    --start-group "$<TARGET_FILE:spyro_platform_ps4>" "$<TARGET_FILE:spyro_game>"
      "$<TARGET_FILE:spyro_platform_common>" --end-group
    -lc -lm -lkernel -lSceSysmodule -lSceVideoOut -lScePad -lSceUserService -lSceAudioOut
    -lScePigletv2VSH
    -o "${PS4_ELF}"
  COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/tools/ps4/oelf_check.py" "${PS4_ELF}"
  DEPENDS spyro_platform_ps4 spyro_game spyro_platform_common
    "$<TARGET_FILE:spyro_platform_ps4>" "$<TARGET_FILE:spyro_game>"
    "$<TARGET_FILE:spyro_platform_common>" "${CMAKE_SOURCE_DIR}/tools/ps4/oelf_check.py"
  COMMENT "PS4: linking spyro.elf (ld.lld, OpenOrbis link.x)"
  VERBATIM)
set(PS4_PKG "${CMAKE_BINARY_DIR}/pkg/IV0000-SPYT00002_00-SPYROPS4GAME0000.pkg")
add_custom_command(OUTPUT "${PS4_PKG}"
  COMMAND ${CMAKE_COMMAND} -E env "OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN}" "PS4_OOBIN=${PS4_OOBIN}"
    bash "${CMAKE_SOURCE_DIR}/tools/ps4/pkg.sh" "${PS4_ELF}" "${CMAKE_BINARY_DIR}/pkg"
  DEPENDS "${PS4_ELF}" "${CMAKE_SOURCE_DIR}/tools/ps4/pkg.sh"
    "${CMAKE_SOURCE_DIR}/tools/ps4/spike/icon.py"
  COMMENT "PS4: eboot.bin and the fpkg (create-fself, create-gp4, PkgTool)"
  VERBATIM)
add_custom_target(spyro_ps4 ALL DEPENDS "${PS4_PKG}")
