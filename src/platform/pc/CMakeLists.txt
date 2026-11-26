# PC build (docs/pc-build.md, D240): the same game and src/platform/common
# as the other hosts, this platform layer (on Encore's platform interface
# and its SDL3 PC backend, D247; OpenGL 3.3), and linux32's
# software rasterizer, PNG writer, log and save-state glue, as the PS4's.
#   tools/pc/tc cmake -S . -B build/pc-linux -G Ninja -DSPYRO_HOST=pc \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/pc-linux.toolchain.cmake
#   tools/pc/tc cmake --build build/pc-linux     -> build/pc-linux/spyro
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3-static)
set(LINUX32_DIR "${CMAKE_SOURCE_DIR}/src/platform/linux32")
file(GLOB PC_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
# Encore's low image and Windows shims, vendored (third_party/encore, D242)
set(ENCORE_DIR "${CMAKE_SOURCE_DIR}/third_party/encore")
set(LOW_IMAGE_DIR "${ENCORE_DIR}/lp64/runtime/low_image")
set(WINPOSIX_DIR "${ENCORE_DIR}/targets/pc/runtime/winposix")
# Encore's platform interface and its PC backend (D247), a library of its
# own: plain C with Encore's flags, not the game's (no -fno-builtin, no
# -funsigned-char), so it is built as Encore's own tests build it
set(ENCORE_CORE_DIR "${ENCORE_DIR}/core")
set(ENCORE_PC_DIR "${ENCORE_DIR}/targets/pc/encore")
add_library(encore_pc STATIC "${ENCORE_PC_DIR}/enc_pc.c" "${ENCORE_CORE_DIR}/src/enc_common.c"
  "${ENCORE_CORE_DIR}/src/enc_blit.c" "${ENCORE_CORE_DIR}/src/enc_event.c")
if(WIN32)
  target_sources(encore_pc PRIVATE "${ENCORE_PC_DIR}/enc_co_win.c")
else()
  target_sources(encore_pc PRIVATE "${ENCORE_CORE_DIR}/src/enc_co.c")
endif()
target_include_directories(encore_pc PUBLIC "${ENCORE_CORE_DIR}/include" "${ENCORE_PC_DIR}")
target_compile_options(encore_pc PRIVATE -O2 -Wall)
target_link_libraries(encore_pc PUBLIC SDL3::SDL3-static)
if(WIN32)
  set(PC_ENCORE_SRCS "${LOW_IMAGE_DIR}/lp64_low_image_win.c" "${WINPOSIX_DIR}/enc_winposix.c")
elseif(APPLE)
  set(PC_ENCORE_SRCS) # the offset model: src/platform/common/p32_image.c (low.c)
else()
  set(PC_ENCORE_SRCS "${LOW_IMAGE_DIR}/lp64_low_image_dyn.c")
endif()
add_executable(spyro MACOSX_BUNDLE ${PC_SRCS} ${PC_ENCORE_SRCS}
  "${LINUX32_DIR}/gpu_soft.c" "${LINUX32_DIR}/png.c" "${LINUX32_DIR}/log.c"
  "${LINUX32_DIR}/savestate_host.c"
  # the options menu's GAME rows (skipping the logos, the cheats; D266): the
  # PS4's, plain C over src/game/cheats_port.c, shared as they are
  "${CMAKE_SOURCE_DIR}/src/platform/ps4/menu_game.c")
set_target_properties(spyro PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# gpu=0's software rasterizer next to the GPU renderer (D243): its GR_*
# entry points renamed soft_*, as in the Vita build (gpu.c dispatches)
set_source_files_properties("${LINUX32_DIR}/gpu_soft.c" PROPERTIES
  COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/src/platform/vita/gpu_soft_names.h")
target_include_directories(spyro PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${LOW_IMAGE_DIR}" "${WINPOSIX_DIR}"
  "${LINUX32_DIR}" "${CMAKE_SOURCE_DIR}/src/platform/common" "${CMAKE_SOURCE_DIR}/src/game")
target_compile_options(spyro PRIVATE ${SPYRO_HOST_FLAGS} -Wall -Wno-unused-function)
target_link_libraries(spyro PRIVATE spyro_game spyro_platform_common psycross_headers
  encore_pc SDL3::SDL3-static)
target_link_options(spyro PRIVATE ${SPYRO_LINK_FLAGS})
# The renderers' hand C is reached only through linux32's wrappers, so its
# entries are pulled in by name (src/platform/linux32/CMakeLists.txt).
foreach(f func_80050BD0 func_800258F0 func_8001F158 func_8001F798 func_800208FC func_80020F34
          func_80022A2C func_80023AC4 func_80059F8C func_80059A48 func_80058D64 func_8004EBA8)
  if(APPLE)
    target_link_options(spyro PRIVATE "-Wl,-u,_${f}") # ld64's spelling, Mach-O's name
  else()
    target_link_options(spyro PRIVATE "-Wl,--undefined=${f}")
  endif()
endforeach()
if(WIN32)
  # A fixed base of 0x400000, no ASLR, and not large-address-aware: the whole
  # process below 2 GB (low.c, Encore's lp64_low_image_win.c). A GUI program (no console window): the
  # log is spyro.log in the user folder, and stderr still reaches a redirect.
  target_link_options(spyro PRIVATE -Wl,--image-base,0x400000 -Wl,--disable-dynamicbase
    -Wl,--disable-high-entropy-va "-Wl,-Xlink=-largeaddressaware:no" -static -mwindows)
  target_link_libraries(spyro PRIVATE pthread) # winpthreads (cdnative.c's worker, semaphores)
elseif(APPLE)
  # macOS (D250 O4): a PIE (every arm64 program), slid above __PAGEZERO's
  # 4 GB; an app bundle, Spyro.app, signed ad hoc (the linker's own
  # signature, kept by the bundle's re-sign below). SDL3 brings its frameworks.
  include("${CMAKE_SOURCE_DIR}/cmake/version.cmake") # 01.22 -> 1.2.2
  string(REGEX REPLACE "^0*([0-9]+)\\.([0-9])([0-9])$" "\\1.\\2.\\3" SPYRO_VERSION "${SPYRO_APP_VER}")
  set_target_properties(spyro PROPERTIES OUTPUT_NAME Spyro
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "Spyro" MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.shayvd.spyro"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${SPYRO_VERSION}" MACOSX_BUNDLE_BUNDLE_VERSION "${SPYRO_VERSION}")
  add_custom_command(TARGET spyro POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:spyro>"
    COMMENT "Spyro.app: signed ad hoc")
else()
  # non-PIE at 0x400000; the C++ runtime static (SDL's HIDAPI is C++-free,
  # the rest of the game C): only glibc, libm and libdl from the system
  target_link_options(spyro PRIVATE -no-pie -Wl,-z,noexecstack)
  target_link_libraries(spyro PRIVATE m pthread dl)
endif()
