# vpk_add_app: a linked Vita executable target packed as a VPK, refusing a link that lost its process parameters.
# The same commands as vita/tools/vpk_pack.py (kh1-decomp's packing), in place of vitasdk's vita_create_self +
# vita_create_vpk (spyro-vita's packing, from its src/platform/vita/CMakeLists.txt @ 987363e).
#
#   include(<kit>/vita/cmake/VpkApp.cmake)        after vitasdk's toolchain file (it sets VITA_ELF_CREATE ...)
#   vpk_add_app(<executable target>
#     VPK <out.vpk>                 relative to CMAKE_CURRENT_BINARY_DIR, or absolute
#     TITLE_ID <ABCD00001> VERSION <##.##> NAME <title>
#     [FILE <src> <dest in the VPK> ...]          sce_sys/icon0.png, sce_sys/livearea/contents/..., README.md ...
#     [DEPENDS <target> ...])                     targets that make FILE sources
#
# Makes the target <executable target>-vpk (in ALL). Its steps, with the work files in <out.vpk>.d/:
#   1. vpk_check_procparams.cmake: arm-vita-eabi-nm must list sceUserMainThreadStackSize and _newlib_heap_size_user
#      (the SDK reads both by name; LTO drops them unless marked used, and the app then stops at launch, no log)
#   2. vita-elf-create, 3. vita-make-fself -s -c,
#   4. vita-mksfoex -s APP_VER -s TITLE_ID -d ATTRIBUTE2=12 (the extended memory budget), 5. vita-pack-vpk
include_guard(GLOBAL)
set(VPK_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(vpk_add_app elf)
  cmake_parse_arguments(A "" "VPK;TITLE_ID;VERSION;NAME" "FILE;DEPENDS" ${ARGN})
  if(A_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "vpk_add_app: unknown arguments ${A_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT TARGET ${elf})
    message(FATAL_ERROR "vpk_add_app: ${elf} is not a target")
  endif()
  foreach(k VPK TITLE_ID VERSION NAME)
    if(NOT A_${k})
      message(FATAL_ERROR "vpk_add_app: ${k} is required")
    endif()
  endforeach()
  if(NOT A_TITLE_ID MATCHES "^[A-Z][A-Z][A-Z][A-Z][0-9][0-9][0-9][0-9][0-9]$")
    message(FATAL_ERROR "vpk_add_app: TITLE_ID ${A_TITLE_ID} is not four capitals and five digits (ABCD00001)")
  endif()
  if(NOT A_VERSION MATCHES "^[0-9][0-9]\\.[0-9][0-9]$")
    message(FATAL_ERROR "vpk_add_app: VERSION ${A_VERSION} is not ##.##")
  endif()
  foreach(t VITA_ELF_CREATE VITA_MAKE_FSELF VITA_MKSFOEX VITA_PACK_VPK CMAKE_NM)
    if(NOT ${t})
      message(FATAL_ERROR "vpk_add_app: ${t} is not set: configure with vitasdk's share/vita.toolchain.cmake")
    endif()
  endforeach()
  list(LENGTH A_FILE n)
  math(EXPR odd "${n} % 2")
  if(odd)
    message(FATAL_ERROR "vpk_add_app: FILE takes <src> <dest> pairs")
  endif()

  get_filename_component(vpk "${A_VPK}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  set(d "${vpk}.d")
  set(stamp "${d}/procparams.ok")
  set(velf "${d}/${elf}.velf")
  set(eboot "${d}/eboot.bin")
  set(sfo "${d}/param.sfo")

  set(add "")
  set(srcs "")
  while(A_FILE)
    list(POP_FRONT A_FILE src dest)
    get_filename_component(src "${src}" ABSOLUTE)
    list(APPEND add -a "${src}=${dest}")
    list(APPEND srcs "${src}")
  endwhile()

  add_custom_command(OUTPUT "${stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${d}"
    COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DELF=$<TARGET_FILE:${elf}>" "-DSTAMP=${stamp}"
            -P "${VPK_CMAKE_DIR}/vpk_check_procparams.cmake"
    DEPENDS ${elf} "${VPK_CMAKE_DIR}/vpk_check_procparams.cmake"
    COMMENT "Checking ${elf}'s process parameters" VERBATIM)
  add_custom_command(OUTPUT "${velf}"
    COMMAND "${VITA_ELF_CREATE}" "$<TARGET_FILE:${elf}>" "${velf}"
    DEPENDS "${stamp}" ${elf}
    COMMENT "vita-elf-create ${elf}" VERBATIM)
  add_custom_command(OUTPUT "${eboot}"
    COMMAND "${VITA_MAKE_FSELF}" -s -c "${velf}" "${eboot}"
    DEPENDS "${velf}"
    COMMENT "vita-make-fself ${elf}" VERBATIM)
  add_custom_command(OUTPUT "${sfo}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${d}"
    COMMAND "${VITA_MKSFOEX}" -s "APP_VER=${A_VERSION}" -s "TITLE_ID=${A_TITLE_ID}" -d ATTRIBUTE2=12
            "${A_NAME}" "${sfo}"
    COMMENT "vita-mksfoex ${A_TITLE_ID} ${A_VERSION}" VERBATIM)
  add_custom_command(OUTPUT "${vpk}"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${vpk}"
    COMMAND "${VITA_PACK_VPK}" -s "${sfo}" -b "${eboot}" ${add} "${vpk}"
    DEPENDS "${sfo}" "${eboot}" ${srcs}
    COMMENT "vita-pack-vpk ${A_VPK}" VERBATIM)
  add_custom_target(${elf}-vpk ALL DEPENDS "${vpk}")
  if(A_DEPENDS)
    add_dependencies(${elf}-vpk ${A_DEPENDS})
  endif()
endfunction()
