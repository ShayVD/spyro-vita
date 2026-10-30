# All 37 overlays, linked statically (docs/porting-plan.md §4.2): each
# overlay's sources are preprocessed with -DOVERLAY_NAME=<ovl> -DIS_<ovl>
# and stripped to the overlay's moby/particle classes, like the decomp's
# Makefile does. SetOverlayPointers() then dispatches by level.
set(OVL_TOOL "${CMAKE_SOURCE_DIR}/tools/overlays/port_overlays.py")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${OVL_TOOL}" list "${S1_DIR}"
  OUTPUT_VARIABLE ovl_rows RESULT_VARIABLE rc OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "port_overlays.py list failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${S1_DIR}/overlays/overlays.yaml" "${S1_DIR}/overlays/mobys.yaml"
  "${S1_DIR}/overlays/particles.yaml" "${OVL_TOOL}")

set(OVL_CPP_FLAGS -m32 -E ${SPYRO_GUARD_FLAGS}
  -DMODERN_COMPILER -DNDEBUG -DNNOCD -DSPYRO_PORT
  -DUSE_EXTENDED_PRIM_POINTERS=0 -DUSE_PGXP=0
  -I${CMAKE_SOURCE_DIR}/src/compat -I${S1_DIR}/include
  -I${PSYCROSS_DIR}/include -I${PSYCROSS_DIR}/include/psx
  -include ${CMAKE_SOURCE_DIR}/src/compat/port_prefix.h)

set(OVL_SRCS)
string(REPLACE "\n" ";" ovl_rows "${ovl_rows}")
foreach(row ${ovl_rows})
  string(REGEX MATCH "^([^|]+)\\|([^|]+)\\|(.*)$" _ "${row}")
  set(ovl "${CMAKE_MATCH_1}")
  set(file "${CMAKE_MATCH_2}")
  set(cls "${CMAKE_MATCH_3}")
  get_filename_component(stem "${file}" NAME_WE)
  set(out "${GEN_DIR}/ovl/${ovl}/${stem}.c")
  add_custom_command(OUTPUT "${out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN_DIR}/ovl/${ovl}"
    COMMAND "${Python3_EXECUTABLE}" "${OVL_TOOL}" preprocess
      --classes "${cls}" --out "${out}" "${S1_DIR}"
      -- "${CMAKE_C_COMPILER}" ${OVL_CPP_FLAGS} -DOVERLAY_NAME=${ovl} -DIS_${ovl}
         "${S1_DIR}/src/overlays/${file}"
    DEPENDS "${S1_DIR}/src/overlays/${file}" "${OVL_TOOL}"
    IMPLICIT_DEPENDS C "${S1_DIR}/src/overlays/${file}"
    COMMENT "overlay ${ovl}: ${file}"
    VERBATIM)
  list(APPEND OVL_SRCS "${out}")
endforeach()

add_custom_command(OUTPUT "${GEN_DIR}/ovl_table.c"
  COMMAND "${Python3_EXECUTABLE}" "${OVL_TOOL}" table "${S1_DIR}"
    "${SPYRO_MATCHING_DIR}/build/psx.map" "${GEN_DIR}/ovl_table.c"
  DEPENDS "${OVL_TOOL}" "${SPYRO_MATCHING_DIR}/build/psx.map"
  COMMENT "overlay table from psx.map")

add_library(spyro_game_ovl OBJECT ${OVL_SRCS} "${GEN_DIR}/ovl_table.c")
target_include_directories(spyro_game_ovl PRIVATE "${CMAKE_SOURCE_DIR}/src/platform/common")
target_compile_options(spyro_game_ovl PRIVATE ${SPYRO_HOST_FLAGS} -fcommon -w)
