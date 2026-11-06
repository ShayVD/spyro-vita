#!/bin/sh
# tests/fill's test built with the Vita's compiler (vitasdk/vitasdk, the
# flags of the Vita build: -O2, its default Cortex-A9 Thumb-2 with NEON),
# so psx_fill16's NEON stores, run under qemu-arm (the spyro-host image):
# 1 M random rects and the edge cases against the plain loop (D111).
#   tests/fill/arm_check.sh
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/fill-arm
docker run --rm -v "$PWD:/work" -w /work vitasdk/vitasdk sh -ec '
  arm-vita-eabi-gcc -O2 -funsigned-char -fno-strict-aliasing -ffp-contract=off \
    -ffreestanding -nostdlib -static -DFILL_FREESTANDING -Isrc/platform/common \
    -Wl,-Ttext=0x10000 -Wl,-e,_start -o build/fill-arm/test_fill \
    tests/fill/arm_start.c tests/fill/test_fill.c -lgcc
  arm-vita-eabi-objdump -d build/fill-arm/test_fill | grep -cE "\svst1\.16\s" \
    | sed "s/^/NEON stores in the test: /"'
docker run --rm -v "$PWD:/work" -w /work spyro-host qemu-arm build/fill-arm/test_fill
