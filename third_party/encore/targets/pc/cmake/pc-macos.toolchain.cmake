# A PC build for macOS on Apple silicon, natively, with Apple's clang (targets/pc/README.md "macOS"): the offset model
# (lp64/cmake/macos.toolchain.cmake, included), SDL3 static (targets/pc/tools/sdl3_macos.sh), and the backend's GL
# request (3.3 core, forward-compatible), which macOS answers with its 4.1 core context: `#version 330` shaders run
# as they are. The game makes its executable a bundle (MACOSX_BUNDLE, targets/pc/macos/Info.plist.in) and signs it ad
# hoc; targets/pc/cmake/test is a stand-in that does.
#   targets/pc/tools/sdl3_macos.sh
#   cmake -S . -B build/pc-macos -G Ninja -DCMAKE_TOOLCHAIN_FILE=<kit>/targets/pc/cmake/pc-macos.toolchain.cmake
# ENC_SDL3_PREFIX is SDL3's install prefix (default ./build/sdl3-macos, where sdl3_macos.sh puts it); find_package
# (SDL3 CONFIG) finds it there. ENC_PC_TARGET is the clang target, for the game's own compiler calls.
# From spyro-vita cmake/pc-macos.toolchain.cmake @ 36d9e07 (MIT; D265). Its SPYRO_ names are ENC_ here.
include("${CMAKE_CURRENT_LIST_DIR}/../../../lp64/cmake/macos.toolchain.cmake")
set(ENC_PC_TARGET arm64-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET})
set(ENC_SDL3_PREFIX "${CMAKE_SOURCE_DIR}/build/sdl3-macos" CACHE PATH "SDL3's install prefix")
list(APPEND CMAKE_PREFIX_PATH "${ENC_SDL3_PREFIX}")
