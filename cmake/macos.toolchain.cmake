# arm64 macOS with Apple's clang, native on an Apple-silicon Mac: the offset
# model's host as a Mach-O program (D250 O4). Encore's toolchain
# (third_party/encore/lp64/cmake/macos.toolchain.cmake, D279); spyro forces
# its own src/compat/p32.h (CMakeLists.txt), so not the kit's header:
#   cmake -S . -B build/macos -G Ninja -DSPYRO_HOST=macos \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/macos.toolchain.cmake
set(LP64_P32_H "" CACHE FILEPATH "spyro forces src/compat/p32.h (CMakeLists.txt)")
include("${CMAKE_CURRENT_LIST_DIR}/../third_party/encore/lp64/cmake/macos.toolchain.cmake")
