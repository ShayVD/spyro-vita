# 32-bit Linux host debug build: headless, deterministic virtual vblanks,
# software renderer for the GR_* layer, PNG screenshots.
file(GLOB LINUX32_SRCS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Platform layer without the game, so tests can link it too.
list(FILTER LINUX32_SRCS EXCLUDE REGEX "/main\\.c$")
add_library(spyro_platform_linux32 STATIC ${LINUX32_SRCS})
target_include_directories(spyro_platform_linux32 PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/platform/common")
target_include_directories(spyro_platform_linux32 PRIVATE
  "${CMAKE_SOURCE_DIR}/src/game") # present.h only
target_link_libraries(spyro_platform_linux32 PUBLIC spyro_platform_common)
target_compile_options(spyro_platform_linux32 PRIVATE ${SPYRO_HOST_FLAGS})

add_executable(spyro main.c)
target_link_libraries(spyro PRIVATE spyro_game spyro_platform_linux32
  spyro_platform_common m pthread)
target_include_directories(spyro PRIVATE "${CMAKE_SOURCE_DIR}/src/game")
target_compile_options(spyro PRIVATE ${SPYRO_HOST_FLAGS})
target_link_options(spyro PRIVATE -m32 -no-pie)
# --prof-draw (prof_draw.c): the renderers GamestateDraw calls, timed
target_link_options(spyro PRIVATE
  -Wl,--wrap=GamestateUpdate,--wrap=func_800258F0,--wrap=func_8001F158
  -Wl,--wrap=func_8001F798,--wrap=func_800208FC,--wrap=func_80020F34
  -Wl,--wrap=func_80022A2C,--wrap=func_80023AC4,--wrap=func_80059F8C
  -Wl,--wrap=func_80059A48,--wrap=func_80058D64,--wrap=func_80058BA8
  -Wl,--wrap=func_800573C8,--wrap=func_80050BD0,--wrap=func_8004EBA8
  # The cyclorama set-up's hand C (cyclorama_hand.c, D94) is reached only
  # through its wrapper, from a library linked after the game's: pull it in.
  -Wl,--undefined=func_80050BD0)
if(SPYRO_NATIVE_MEM)
  # Without the mips2c units (D100) the renderers' hand C is reached only
  # through these wrappers too.
  target_link_options(spyro PRIVATE
    -Wl,--undefined=func_800258F0,--undefined=func_8001F158,--undefined=func_8001F798
    -Wl,--undefined=func_800208FC,--undefined=func_80020F34,--undefined=func_80022A2C
    -Wl,--undefined=func_80023AC4,--undefined=func_80059F8C,--undefined=func_80059A48
    -Wl,--undefined=func_80058D64,--undefined=func_8004EBA8)
endif()
