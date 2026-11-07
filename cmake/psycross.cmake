# PsyCross (MIT), from the patched copy (patches/PsyCross). Since stage 8
# (docs/native-plan.md, "The stage 8 plan"; D113-D118) the native and Vita
# builds compile none of its code: libcd, libspu, libmcrd and libetc are ours
# (D104 and earlier), libpad (8a, D114), libgpu (8b, D115), libgte with the
# GTE register file (8c, D116) and the OT walk (8d, D117) too. Two things
# stay:
#  - its headers (include/psx: the PsyQ API the decomp and our code compile
#    against), behind our patches: psycross_headers. The owner's choice, A,
#    in D118; MIT, credited in THIRD_PARTY_NOTICES.md;
#  - its GTE and OT walk as the checks' reference, psycross_ref, below.
# PS1-native primitive layout (docs/porting-plan.md §5): no extended prim
# pointers, no PGXP.
add_library(psycross_headers INTERFACE)
target_include_directories(psycross_headers INTERFACE
  "${PSYCROSS_DIR}/include" "${PSYCROSS_DIR}/include/psx")
target_compile_definitions(psycross_headers INTERFACE
  USE_EXTENDED_PRIM_POINTERS=0 USE_PGXP=0)

set(PSYCROSS_GTE_SRCS "${PSYCROSS_DIR}/src/psx/LIBGTE.C" "${PSYCROSS_DIR}/src/psx/INLINE_C.C"
  "${PSYCROSS_DIR}/src/gte/PsyX_GTE.cpp")
set(PSYCROSS_WALK_SRCS "${PSYCROSS_DIR}/src/gpu/PsyX_GPU.cpp")
set_source_files_properties(${PSYCROSS_GTE_SRCS} ${PSYCROSS_WALK_SRCS} PROPERTIES LANGUAGE CXX)

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
option(SPYRO_PSYCROSS_REF "Build PsyCross's GTE and OT walk as the reference library psycross_ref" ${psycross_ref_default})
if(NOT SPYRO_NATIVE_MEM AND NOT SPYRO_PSYCROSS_REF)
  message(FATAL_ERROR "the mips2c units call GTE_operator: SPYRO_PSYCROSS_REF needs to be ON without SPYRO_NATIVE_MEM")
endif()
if(SPYRO_PSYCROSS_REF)
  add_library(psycross_ref STATIC ${PSYCROSS_GTE_SRCS} ${PSYCROSS_WALK_SRCS})
  target_link_libraries(psycross_ref PUBLIC psycross_headers)
  target_include_directories(psycross_ref PRIVATE "${PSYCROSS_DIR}/src")
  set(psycross_ref_names InitGeom SetGeomOffset SetGeomScreen SetTransMatrix MulMatrix MulMatrix0
      MTC2 MFC2 CTC2 CFC2
      # PsyX_GPU.cpp's (walk_ref.h declares what the checks use)
      prim_terminator currentDispEnv activeDispEnv activeDrawEnv overrideTexture
      overrideTextureWidth overrideTextureHeight g_GPUDisabledState g_DrawPrimMode
      g_vertexBuffer g_splits g_vertexIndex g_splitIndex ClearSplits
      g_psyxPolyHook DrawAllSplits g_psyxNextPacket
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
  target_compile_definitions(psycross_ref PRIVATE PSYX_NO_SDL)
  target_compile_options(psycross_ref PRIVATE ${SPYRO_GUARD_FLAGS} -w
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-narrowing>)
endif()
