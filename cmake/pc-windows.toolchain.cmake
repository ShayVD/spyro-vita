# The PC build for x86-64 Windows (docs/pc-build.md, D241), cross-compiled
# with llvm-mingw (clang for x86_64-w64-mingw32, the UCRT) inside tools/pc/tc
# (tools/docker/pc.Dockerfile):
#   cmake -S . -B build/pc-win -G Ninja -DSPYRO_HOST=pc \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/pc-windows.toolchain.cmake
# lld's MinGW driver can't make the relocatable objects the presenter and the
# 60 Hz update are linked into (-r; cmake/presenter.cmake, tick60.cmake), so
# those links use GNU ld for PE (binutils-mingw-w64), and GNU objcopy edits
# their symbols; the executable itself is lld's.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(SPYRO_PC_TARGET x86_64-w64-mingw32)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-clang++)
set(CMAKE_ASM_COMPILER x86_64-w64-mingw32-clang)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_AR /opt/llvm-mingw/bin/llvm-ar CACHE FILEPATH "")
set(CMAKE_RANLIB /opt/llvm-mingw/bin/llvm-ranlib CACHE FILEPATH "")
set(CMAKE_NM /opt/llvm-mingw/bin/llvm-nm CACHE FILEPATH "")
set(CMAKE_OBJCOPY /usr/bin/x86_64-w64-mingw32-objcopy CACHE FILEPATH "")
set(SPYRO_PC_RELOC_LD_FLAGS -fuse-ld=/usr/bin/x86_64-w64-mingw32-ld)
set(SPYRO_SDL3_PREFIX /opt/sdl3-win CACHE PATH "SDL3's install prefix")
list(APPEND CMAKE_PREFIX_PATH "${SPYRO_SDL3_PREFIX}")
set(CMAKE_FIND_ROOT_PATH /opt/llvm-mingw/x86_64-w64-mingw32 "${SPYRO_SDL3_PREFIX}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_COMPILER_LAUNCHER timeout 300)
