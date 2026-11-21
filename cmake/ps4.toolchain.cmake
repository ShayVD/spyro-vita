# The PS4 (docs/ps4-plan.md P3, decisions D193): clang 18 for
# x86_64-pc-freebsd12-elf against OpenOrbis v0.5.4's sysroot, as the P2 spike
# builds (tools/ps4/spike/build.sh, D180). The tools come from the
# environment, set by the arm64 Docker image or by `tools/ps4/sdk.sh --env`:
#   eval "$(tools/ps4/sdk.sh --env)"
#   cmake -S . -B build/ps4 -G Ninja -DSPYRO_HOST=ps4 \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/ps4.toolchain.cmake
# CMake builds the static libraries; src/platform/ps4/CMakeLists.txt links the
# eboot with ld.lld and OpenOrbis's link.x itself, as the spike does.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
foreach(v OO_PS4_TOOLCHAIN PS4_CC PS4_LD PS4_OOBIN)
  if(NOT DEFINED ENV{${v}})
    message(FATAL_ERROR "${v} is not set: eval \"$(tools/ps4/sdk.sh --env)\" first (docs/ps4-build.md)")
  endif()
  set(${v} "$ENV{${v}}" CACHE STRING "")
endforeach()
get_filename_component(ps4_llvm_bin "${PS4_CC}" DIRECTORY)
set(CMAKE_C_COMPILER "${PS4_CC}")
set(CMAKE_CXX_COMPILER "${ps4_llvm_bin}/clang++")
set(CMAKE_ASM_COMPILER "${PS4_CC}")
foreach(lang C CXX ASM)
  set(CMAKE_${lang}_COMPILER_TARGET x86_64-pc-freebsd12-elf)
endforeach()
set(CMAKE_AR "${ps4_llvm_bin}/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${ps4_llvm_bin}/llvm-ranlib" CACHE FILEPATH "")
set(CMAKE_NM "${ps4_llvm_bin}/llvm-nm" CACHE FILEPATH "")
set(CMAKE_OBJCOPY "${ps4_llvm_bin}/llvm-objcopy" CACHE FILEPATH "")
set(CMAKE_SYSROOT "${OO_PS4_TOOLCHAIN}")
set(ps4_sys -isystem "${OO_PS4_TOOLCHAIN}/include")
string(JOIN " " ps4_sys_s ${ps4_sys})
set(CMAKE_C_FLAGS_INIT "${ps4_sys_s}")
set(CMAKE_CXX_FLAGS_INIT "-isystem ${OO_PS4_TOOLCHAIN}/include/c++/v1 ${ps4_sys_s}")
set(CMAKE_ASM_FLAGS_INIT "${ps4_sys_s}")
# no executable links in the configure checks: the eboot's link is ours
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH "${OO_PS4_TOOLCHAIN}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
