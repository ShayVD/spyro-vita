# PsyCross (MIT) as a static library, built from the patched copy.
# PS1-native primitive layout (docs/porting-plan.md §5): no extended prim
# pointers, no PGXP. Our own libcd and SPU replace LIBCD.C, LIBSPU.C and the
# OpenAL backend (§6), our file-backed libmcrd replaces LIBMCRD.C, and our
# libetc (VSync over the port's own clock, stage 7b, D104) replaces LIBETC.C,
# our libpad (stage 8a, D114) LIBPAD.C, our libgpu (stage 8b, D115) LIBGPU.C,
# and our walk (stage 8d, D117: src/platform/common/gpu_walk.c) PsyX_GPU.cpp.
# The platform layer (window, timing, pad, GR_* renderer)
# comes from src/platform/<host> unless SPYRO_PSYCROSS_PLATFORM is ON.
option(SPYRO_PSYCROSS_PLATFORM "Use PsyCross's own SDL2/GL platform layer" OFF)

file(GLOB PSYCROSS_SRCS
  "${PSYCROSS_DIR}/src/psx/*.C"
  "${PSYCROSS_DIR}/src/gte/*.cpp"
  "${PSYCROSS_DIR}/src/gpu/*.cpp")
list(FILTER PSYCROSS_SRCS EXCLUDE REGEX "/(LIBCD|LIBSPU|LIBMCRD|LIBETC|LIBPAD|LIBGPU)\\.C$")
if(SPYRO_PSYCROSS_PLATFORM)
  list(APPEND PSYCROSS_SRCS
    "${PSYCROSS_DIR}/src/PsyX_main.cpp"
    "${PSYCROSS_DIR}/src/pad/PsyX_pad.cpp"
    "${PSYCROSS_DIR}/src/render/PsyX_render.cpp"
    "${PSYCROSS_DIR}/src/render/glad.c")
endif()
# libgte, the GTE register file and its moves are ours since stage 8c
# (src/game/libgte.c, gte_regs.c; D116): PsyCross's GTE is only the
# reference below.
set(PSYCROSS_GTE_SRCS "${PSYCROSS_DIR}/src/psx/LIBGTE.C" "${PSYCROSS_DIR}/src/psx/INLINE_C.C"
  "${PSYCROSS_DIR}/src/gte/PsyX_GTE.cpp")
# The OT walk, its splits and the GPU state are ours since stage 8d
# (gpu_walk.c, D117): PsyCross's walk is only the reference below.
set(PSYCROSS_WALK_SRCS "${PSYCROSS_DIR}/src/gpu/PsyX_GPU.cpp")
list(REMOVE_ITEM PSYCROSS_SRCS ${PSYCROSS_GTE_SRCS} ${PSYCROSS_WALK_SRCS})
set_source_files_properties(${PSYCROSS_SRCS} ${PSYCROSS_GTE_SRCS} ${PSYCROSS_WALK_SRCS}
  PROPERTIES LANGUAGE CXX)
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

# The reference (docs/native-plan.md, the stage 8 plan, D113; D116, D117):
# PsyCross's GTE beside ours, for tests/gte, which compares the two call by
# call, and for the mips2c units of the arena build, which call its
# GTE_operator at fast_render=0. Its libgte and register moves are renamed
# psyx_*; GTE_operator keeps its name and works on our gteRegs and RTP hooks
# (patch 0015 drops PsyCross's definitions of them). Also PsyCross's OT walk
# (PsyX_GPU.cpp), every symbol of it renamed psyx_*, beside ours (gpu_walk.c)
# for the host's walk checks (src/game/walk_check.c, native_draw_check.c);
# its fill, DR_MOVE and DR_LOAD go to the check (psyx_ref_ClearImage, ...),
# not to VRAM. Never in the native build's game.
if(SPYRO_HOST STREQUAL "linux32" OR NOT SPYRO_NATIVE_MEM)
  set(psycross_ref_default ON)
else()
  set(psycross_ref_default OFF)
endif()
option(SPYRO_PSYCROSS_REF "Build PsyCross's GTE as the reference library psycross_ref" ${psycross_ref_default})
if(NOT SPYRO_NATIVE_MEM AND NOT SPYRO_PSYCROSS_REF)
  message(FATAL_ERROR "the mips2c units call GTE_operator: SPYRO_PSYCROSS_REF needs to be ON without SPYRO_NATIVE_MEM")
endif()
if(SPYRO_PSYCROSS_REF)
  add_library(psycross_ref STATIC ${PSYCROSS_GTE_SRCS} ${PSYCROSS_WALK_SRCS})
  target_include_directories(psycross_ref PUBLIC
    "${PSYCROSS_DIR}/include" "${PSYCROSS_DIR}/include/psx")
  target_include_directories(psycross_ref PRIVATE "${PSYCROSS_DIR}/src")
  set(psycross_ref_names InitGeom SetGeomOffset SetGeomScreen SetTransMatrix MulMatrix MulMatrix0
      MTC2 MFC2 CTC2 CFC2
      # PsyX_GPU.cpp's (walk_ref.h declares what the checks use)
      prim_terminator currentDispEnv activeDispEnv activeDrawEnv overrideTexture
      overrideTextureWidth overrideTextureHeight g_GPUDisabledState g_DrawPrimMode
      g_vertexBuffer g_splits g_vertexIndex g_splitIndex g_psyxClearSplitsHook ClearSplits
      g_psyxPolyHook DrawAllSplits g_psyxNextPacket g_psyxPacketHook PsyX_AddSplit
      PsyX_VertexBuffer PsyX_SplitCount PsyX_SplitGet PsyX_ClearSplits PsyX_ParseList
      PsyX_SplitStateSize PsyX_SplitStateSave PsyX_SplitStateRestore
      ParsePrimitivesLinkedList ParsePrimitive)
  foreach(n ClearImage MoveImage)
    target_compile_definitions(psycross_ref PRIVATE ${n}=psyx_ref_${n})
  endforeach()
  # libgpu.h #undefs LoadImage (a Windows guard), so its call is renamed in
  # the archive instead
  add_custom_command(TARGET psycross_ref POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" --redefine-sym LoadImage=psyx_ref_LoadImage
      "$<TARGET_FILE:psycross_ref>"
    COMMAND "${CMAKE_RANLIB}" "$<TARGET_FILE:psycross_ref>"
    VERBATIM)
  foreach(n ${psycross_ref_names})
    target_compile_definitions(psycross_ref PRIVATE ${n}=psyx_${n})
  endforeach()
  target_compile_definitions(psycross_ref PRIVATE PSYX_NO_SDL USE_EXTENDED_PRIM_POINTERS=0 USE_PGXP=0)
  target_compile_options(psycross_ref PRIVATE ${SPYRO_GUARD_FLAGS} -w
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-narrowing>)
endif()
