#!/bin/sh
# raster_mt.c against gpu_soft.c (raster_check.c); needs PsyCross's headers
# patched (any configured build's patched/PsyCross, or the submodule's).
# The headers want 32-bit pointers: the host container's i686 compiler, e.g.
#   tools/tc env CC=i686-linux-gnu-gcc TMPDIR=build tests/ps4_raster/run.sh
# Usage: [CC=cc] [PSX_INC=build/ps4/patched/PsyCross/include] tests/ps4_raster/run.sh
set -e
cd "$(dirname "$0")/../.."
inc=${PSX_INC:-build/ps4/patched/PsyCross/include}
[ -d "$inc" ] || inc=third_party/PsyCross/include
out=${TMPDIR:-/tmp}/ps4_raster_check
flags="-O2 -w -Isrc/platform/linux32 -Isrc/platform/common -Isrc/platform/ps4 -I$inc"
# gpu_soft.c with the soft_ names (the Vita's gpu_soft_names.h, as the PS4 build)
${CC:-cc} $flags -c -o "$out.soft.o" -include src/platform/vita/gpu_soft_names.h \
  src/platform/linux32/gpu_soft.c
${CC:-cc} $flags -o "$out" tests/ps4_raster/raster_check.c src/platform/ps4/raster_mt.c \
  "$out.soft.o" -lpthread
${RUN:-} "$out"
