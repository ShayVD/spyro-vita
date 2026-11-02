#!/bin/sh
# test_spu_native built natively on an arm64 Mac (Apple clang, NEON): the
# NEON path of spu_native.c against its C path and libspu.c's psx_spu_mix.
#   tests/spu/neon_check.sh [VBLANKS [SEED]]
set -e
cd "$(dirname "$0")/../.."
out=${TMPDIR:-/tmp}/test_spu_native_neon
C=src/platform/common
cc -O2 -funsigned-char -fno-strict-aliasing -ffp-contract=off -w -I$C \
  -Ithird_party/PsyCross/include -Ithird_party/PsyCross/include/psx \
  tests/spu/test_spu_native.c $C/libspu.c $C/spu_native.c $C/xa.c -o "$out"
"$out" "$@"
