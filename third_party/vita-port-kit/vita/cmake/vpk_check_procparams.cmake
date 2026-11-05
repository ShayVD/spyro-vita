# cmake -DNM=<arm-vita-eabi-nm> -DELF=<linked elf> -DSTAMP=<file> -P vpk_check_procparams.cmake
# Fails unless the ELF defines sceUserMainThreadStackSize and _newlib_heap_size_user (VpkApp.cmake's step 1; the
# same check as vita/tools/vpk_pack.py). Writes STAMP when they are there.
file(REMOVE "${STAMP}")
execute_process(COMMAND "${NM}" "${ELF}" OUTPUT_VARIABLE syms RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "vpk: ${NM} ${ELF} failed")
endif()
set(lost "")
foreach(s sceUserMainThreadStackSize _newlib_heap_size_user)
  # only a line with an address: without the heap size newlib still leaves "         w _newlib_heap_size_user"
  if(NOT syms MATCHES "(^|\n)[0-9a-fA-F]+ [A-Za-z] ${s}(\n|$)")
    list(APPEND lost ${s})
  endif()
endforeach()
if(lost)
  list(JOIN lost " " lost)
  message(FATAL_ERROR "vpk: ${lost} missing from ${ELF} (dropped by LTO? define them with VPK_PROCESS_PARAMS, "
                      "vita/runtime/procparam, which marks them used)")
endif()
file(WRITE "${STAMP}" "ok\n")
