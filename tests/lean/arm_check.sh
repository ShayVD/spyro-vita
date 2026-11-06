#!/bin/sh
# tests/lean's test built with the Vita's compiler (vitasdk/vitasdk, the
# flags of the Vita build: -O2, its default Cortex-A9 Thumb-2) with the
# ARMv6 SIMD instructions, run under qemu-arm (the spyro-host image):
# 16.8 M random quads and the edge cases against the C rule (D105).
#   tests/lean/arm_check.sh
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/lean-arm
docker run --rm -v "$PWD:/work" -w /work vitasdk/vitasdk sh -ec '
  arm-vita-eabi-gcc -O2 -funsigned-char -fno-strict-aliasing -ffp-contract=off \
    -ffreestanding -nostdlib -static -DLEAN_FREESTANDING -Isrc/platform/common \
    -Wl,-Ttext=0x10000 -Wl,-e,_start -o build/lean-arm/test_lean \
    tests/lean/arm_start.c tests/lean/test_lean.c -lgcc
  arm-vita-eabi-objdump -d build/lean-arm/test_lean | grep -cE "\s(usub8|sel|qsub16|uqsub16|uadd16)\s" \
    | sed "s/^/simd instructions in the test: /"'
docker run --rm -v "$PWD:/work" -w /work spyro-host qemu-arm build/lean-arm/test_lean
