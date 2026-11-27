#!/usr/bin/env bash
# Builds the platform interface's conformance run for the Vita (core/test/conformance.c over enc_vita.c) as an ELF,
# and with ENC_VPK=1 as a VPK through targets/vita/tools/vpk_pack.py. Needs vitasdk ($VITASDK). It is built in CI,
# never run there; running it on a Vita needs the owner's yes first (targets/vita/docs/hardware-runs.md).
#   targets/vita/encore/build_conformance.sh [OUT_DIR]
set -euo pipefail
k=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$k/build/vita-conformance}
: "${VITASDK:?set VITASDK (targets/vita/tools/get_vitasdk.sh fetches one)}"
cc="$VITASDK/bin/arm-vita-eabi-gcc"
rt="$k/targets/vita/runtime"
mkdir -p "$out"
cat > "$out/main.c" <<'C'
#include <psp2/kernel/processmgr.h>
#include "conformance.h"
#include "vpk_procparam.h"
VPK_PROCESS_PARAMS(32 * 1024 * 1024, 512 * 1024);
int main(void) {
  EncPlatformConfig pc = {"encoreconf", "Encore conformance", 0, 0};
  int f = enc_platform_init(&pc) == ENC_OK ? enc_conformance_run() : -1;
  enc_platform_shutdown();
  sceKernelExitProcess(f);
  return 0;
}
C
"$cc" -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fshort-enums -DENC_TLS= \
  -I"$k/core/include" -I"$k/core/test" -I"$rt/log" -I"$rt/audioout" -I"$rt/touch" -I"$rt/vcount" \
  -I"$rt/ucontext/std" -I"$rt/ucontext" -I"$rt/procparam" \
  "$out/main.c" "$k/core/test/conformance.c" "$k/core/src/enc_common.c" "$k/core/src/enc_blit.c" \
  "$k/core/src/enc_disc.c" "$k/core/src/enc_co.c" "$k/core/src/enc_event.c" "$k/targets/vita/encore/enc_vita.c" \
  "$rt/log/vpk_log.c" "$rt/audioout/vpk_audio_out.c" "$k/core/src/enc_audio_ring.c" "$rt/touch/vpk_touch.c" \
  "$k/core/src/enc_discimage.c" "$rt/ucontext/vpk_ucontext.S" \
  -Wl,-q -Wl,--defsym=__sce_headroom=0x4000 -lSceDisplay_stub -lSceCtrl_stub -lSceTouch_stub -lSceAudio_stub -lSceSysmem_stub -lSceGxm_stub -lSceCommonDialog_stub -lSceMotion_stub -lScePower_stub -lm \
  -o "$out/encore_conformance.elf"
echo "build_conformance: $out/encore_conformance.elf"
if [ "${ENC_VPK:-0}" = 1 ]; then
  # title ENCR00001; the log goes to ux0:data/encoreconf/log/encoreconf.log and ends "encore conformance: N failed"
  python3 "$k/targets/vita/tools/vpk_pack.py" pack --elf "$out/encore_conformance.elf" --vpk "$out/encore_conformance.vpk" \
    --title-id ENCR00001 --version 01.00 --name "Encore conformance" --placeholder-art --work "$out/vpk-work"
  echo "build_conformance: $out/encore_conformance.vpk"
fi
