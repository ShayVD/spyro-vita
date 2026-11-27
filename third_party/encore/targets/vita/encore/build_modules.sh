#!/usr/bin/env bash
# Builds the portable modules' tests (core/test: ring, audio_ring, discimage, menu, config, the same files
# core/test/run_module_tests.sh runs on host and PC) into one Vita app, as an ELF and with ENC_VPK=1 as a VPK. Each
# test's main is renamed and called in turn; their printf goes to the app's log. Needs vitasdk ($VITASDK). Vita3K
# runs it; a run on a Vita needs the owner's yes first (targets/vita/docs/hardware-runs.md).
#   targets/vita/encore/build_modules.sh [OUT_DIR]
# Title ENCR00002. It writes under ux0:data/encoremod/ and its log, ux0:data/encoremod/log/encoremod.log, ends
# "encore modules: N failed". The discimage image past 3 GiB is left out (DISCIMAGE_NO_BIG): the memory card's exFAT
# has no sparse files.
set -euo pipefail
k=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$k/build/vita-modules}
: "${VITASDK:?set VITASDK (targets/vita/tools/get_vitasdk.sh fetches one)}"
cc="$VITASDK/bin/arm-vita-eabi-gcc"
rt="$k/targets/vita/runtime"
t="$k/core/test"
mkdir -p "$out"
cat > "$out/main.c" <<'C'
#include <psp2/kernel/processmgr.h>
#include <sys/stat.h>
#include "vpk_log.h"
#include "vpk_procparam.h"
VPK_PROCESS_PARAMS(32 * 1024 * 1024, 512 * 1024);
#define D "ux0:data/encoremod"
int ring_main(void);
int audio_ring_main(int argc, char **argv);
int discimage_main(int argc, char **argv);
int menu_main(int argc, char **argv);
int config_main(int argc, char **argv);
int main(void) {
  static char *disc[] = {"discimage", D "/disc", 0}, *menu[] = {"menu", D "/menu", 0},
              *cfg[] = {"config", D "/cfg", 0}, *au[] = {"audio_ring", 0};
  const char *dirs[] = {D, D "/log", D "/disc", D "/menu", D "/cfg"};
  int i, failed = 0;
  for (i = 0; i < (int)(sizeof dirs / sizeof *dirs); i++) mkdir(dirs[i], 0777);
  vpk_log_open(D "/log/encoremod.log", "encore modules on vita\n");
  failed += ring_main() != 0;
  failed += audio_ring_main(1, au) != 0;
  failed += discimage_main(2, disc) != 0;
  failed += menu_main(2, menu) != 0;
  failed += config_main(2, cfg) != 0;
  vpk_log("encore modules: %d failed\n", failed);
  sceKernelExitProcess(failed);
  return 0;
}
C
objs=()
for m in ring audio_ring discimage menu config; do # each test's main renamed, one object each (their statics clash)
  "$cc" -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-format-truncation -DDISCIMAGE_NO_BIG -Dmain=${m}_main \
    -I"$k/core/include" -c "$t/${m}_test.c" -o "$out/${m}_test.o"
  objs+=("$out/${m}_test.o")
done
"$cc" -std=gnu99 -O2 -Wall -Wextra -Werror -I"$k/core/include" -I"$rt/log" -I"$rt/procparam" \
  "$out/main.c" "${objs[@]}" "$rt/log/vpk_log.c" "$rt/log/vpk_log_stdio.c" \
  "$k/core/src/enc_ring.c" "$k/core/src/enc_audio_ring.c" "$k/core/src/enc_discimage.c" "$k/core/src/enc_menu.c" \
  "$k/core/src/enc_menu_panel.c" "$k/core/src/enc_config.c" \
  -Wl,-q -Wl,--defsym=__sce_headroom=0x4000 -lSceSysmem_stub -lm -o "$out/encore_modules.elf"
echo "build_modules: $out/encore_modules.elf"
if [ "${ENC_VPK:-0}" = 1 ]; then
  python3 "$k/targets/vita/tools/vpk_pack.py" pack --elf "$out/encore_modules.elf" --vpk "$out/encore_modules.vpk" \
    --title-id ENCR00002 --version 01.00 --name "Encore modules" --placeholder-art --work "$out/vpk-work"
  echo "build_modules: $out/encore_modules.vpk"
fi
