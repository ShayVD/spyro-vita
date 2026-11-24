# PC build (docs/pc-build.md, D240): the same game and src/platform/common
# as the other hosts, this platform layer (SDL3, OpenGL 3.3), and linux32's
# software rasterizer, PNG writer, log and save-state glue, as the PS4's.
#   tools/pc/tc cmake -S . -B build/pc-linux -G Ninja -DSPYRO_HOST=pc \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/pc-linux.toolchain.cmake
#   tools/pc/tc cmake --build build/pc-linux     -> build/pc-linux/spyro
find_package(SDL3 REQUIRED CONFIG COMPONENTS SDL3-static)
set(LINUX32_DIR "${CMAKE_SOURCE_DIR}/src/platform/linux32")
file(GLOB PC_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_executable(spyro ${PC_SRCS}
  "${LINUX32_DIR}/gpu_soft.c" "${LINUX32_DIR}/png.c" "${LINUX32_DIR}/log.c"
  "${LINUX32_DIR}/savestate_host.c")
set_target_properties(spyro PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# gpu=0's software rasterizer next to the GPU renderer (D243): its GR_*
# entry points renamed soft_*, as in the Vita build (gpu.c dispatches)
set_source_files_properties("${LINUX32_DIR}/gpu_soft.c" PROPERTIES
  COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/src/platform/vita/gpu_soft_names.h")
target_include_directories(spyro PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${LINUX32_DIR}" "${CMAKE_SOURCE_DIR}/src/platform/common" "${CMAKE_SOURCE_DIR}/src/game")
target_compile_options(spyro PRIVATE ${SPYRO_HOST_FLAGS} -Wall -Wno-unused-function)
target_link_libraries(spyro PRIVATE spyro_game spyro_platform_common psycross_headers
  SDL3::SDL3-static)
target_link_options(spyro PRIVATE ${SPYRO_LINK_FLAGS})
# The renderers' hand C is reached only through linux32's wrappers, so its
# entries are pulled in by name (src/platform/linux32/CMakeLists.txt).
foreach(f func_80050BD0 func_800258F0 func_8001F158 func_8001F798 func_800208FC func_80020F34
          func_80022A2C func_80023AC4 func_80059F8C func_80059A48 func_80058D64 func_8004EBA8)
  target_link_options(spyro PRIVATE "-Wl,--undefined=${f}")
endforeach()
if(WIN32)
  # A fixed base of 0x400000, no ASLR, and not large-address-aware: the whole
  # process below 2 GB (low_win.c). A GUI program (no console window): the
  # log is spyro.log in the user folder, and stderr still reaches a redirect.
  target_link_options(spyro PRIVATE -Wl,--image-base,0x400000 -Wl,--disable-dynamicbase
    -Wl,--disable-high-entropy-va "-Wl,-Xlink=-largeaddressaware:no" -static -mwindows)
  target_link_libraries(spyro PRIVATE pthread) # winpthreads (cdnative.c's worker, semaphores)
else()
  # non-PIE at 0x400000; the C++ runtime static (SDL's HIDAPI is C++-free,
  # the rest of the game C): only glibc, libm and libdl from the system
  target_link_options(spyro PRIVATE -no-pie -Wl,-z,noexecstack)
  target_link_libraries(spyro PRIVATE m pthread dl)
endif()
