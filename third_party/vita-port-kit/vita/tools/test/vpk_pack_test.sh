#!/usr/bin/env bash
# vpk_pack.py and vita/cmake/VpkApp.cmake on three tiny apps (needs vitasdk, cmake and ninja; no game data):
#   good  VPK_PROCESS_PARAMS: both pack, and give the same param.sfo values and file list
#   bad   no process parameters at all (newlib still leaves an undefined weak _newlib_heap_size_user): both refuse
#   lto   the two variables without `used`, -flto: the stack size is dropped, both refuse
#   tight good's code with .rodata padded until the text ends 500 bytes before the data segment, less than the
#         ~1.1 KB of SCE data vita-elf-create puts there (gotchas 17): without __sce_headroom both fail "segment 1
#         overlaps" (vpk_pack.py warns first and names the fix); linked with it (SCE_HEADROOM, VPK_SCE_HEADROOM or
#         the flag by hand) both pack, the link line has the flag and arm-vita-eabi-nm shows the symbol
set -euo pipefail
KIT="$(cd "$(dirname "$0")/../../.." && pwd)"
: "${VITASDK:=/usr/local/vitasdk}"
export VITASDK
B="$VITASDK/bin"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
PACK="python3 $KIT/vita/tools/vpk_pack.py"
fail() { echo "FAIL: $*"; exit 1; }

cat > "$T/good.c" <<'EOF'
#include "vpk_procparam.h"
VPK_PROCESS_PARAMS(16 << 20, 1 << 20);
int main(void) { return 0; }
EOF
cat > "$T/bad.c" <<'EOF'
int main(void) { return 0; }
EOF
cat > "$T/lto.c" <<'EOF'
int _newlib_heap_size_user = 16 << 20;
unsigned int sceUserMainThreadStackSize = 1 << 20;
int main(void) { return 0; }
EOF
cat > "$T/tight.c" <<'EOF'
#include "vpk_procparam.h"
VPK_PROCESS_PARAMS(16 << 20, 1 << 20);
__attribute__((used)) static const char pad[PAD] = {1};
int main(void) { return pad[0]; }
EOF
for app in good bad lto; do
  lto=""; [[ $app == lto ]] && lto=-flto
  "$B/arm-vita-eabi-gcc" -O2 $lto -I"$KIT/vita/runtime/procparam" -Wl,-q "$T/$app.c" -o "$T/$app.elf"
done
# tight: one link to measure, then the padding that leaves 500 bytes
gap() { python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import vpk_pack; print(vpk_pack.text_gap(sys.argv[2]))' \
  "$KIT/vita/tools" "$1"; }
"$B/arm-vita-eabi-gcc" -O2 -DPAD=1 -I"$KIT/vita/runtime/procparam" -Wl,-q "$T/tight.c" -o "$T/probe.elf"
PAD=$(( $(gap "$T/probe.elf") - 500 + 1 ))
"$B/arm-vita-eabi-gcc" -O2 -DPAD=$PAD -I"$KIT/vita/runtime/procparam" -Wl,-q "$T/tight.c" -o "$T/tight.elf"
"$B/arm-vita-eabi-gcc" -O2 -DPAD=$PAD -I"$KIT/vita/runtime/procparam" -Wl,-q -Wl,--defsym=__sce_headroom=0x4000 \
  "$T/tight.c" -o "$T/tight_h.elf"
[[ $(gap "$T/tight.elf") == 500 ]] || fail "tight: gap $(gap "$T/tight.elf"), not 500"

# Python
$PACK pack --elf "$T/good.elf" --vpk "$T/py/good.vpk" --title-id VPKT00001 --version 01.02 --name "Kit Test" \
  --placeholder-art --file "$T/good.c=src/good.c" >/dev/null
$PACK show "$T/py/good.vpk" > "$T/py.show"
for want in "sfo APP_VER=01.02" "sfo ATTRIBUTE2=12" "sfo TITLE=Kit Test" "sfo TITLE_ID=VPKT00001" \
            "file eboot.bin " "file sce_sys/param.sfo " "file sce_sys/icon0.png " \
            "file sce_sys/livearea/contents/bg.png " "file sce_sys/livearea/contents/startup.png " \
            "file sce_sys/livearea/contents/template.xml " "file src/good.c "; do
  grep -qF "$want" "$T/py.show" || { cat "$T/py.show"; fail "python: no '$want'"; }
done
for app in bad lto; do
  if $PACK pack --elf "$T/$app.elf" --vpk "$T/py/$app.vpk" --title-id VPKT00001 --version 01.02 --name x \
      > "$T/py-$app.log"; then fail "python packed $app"; fi
  grep -q "sceUserMainThreadStackSize" "$T/py-$app.log" || { cat "$T/py-$app.log"; fail "python $app: message"; }
  [[ ! -e "$T/py/$app.vpk" ]] || fail "python left $app.vpk"
done
grep -q "_newlib_heap_size_user" "$T/py-bad.log" || fail "python bad: the weak heap reference passed"
echo "ok: vpk_pack.py packs good, refuses bad and lto"

# Python, tight: fails with the fix named, packs once linked with the headroom
if $PACK pack --elf "$T/tight.elf" --vpk "$T/py/tight.vpk" --title-id VPKT00001 --version 01.02 --name x \
    > "$T/py-tight.log" 2>&1; then fail "python packed tight without __sce_headroom"; fi
grep -q "segment 1 overlaps" "$T/py-tight.log" || { cat "$T/py-tight.log"; fail "python tight: vita-elf-create's error"; }
grep -q "warning: .* 500 bytes" "$T/py-tight.log" || { cat "$T/py-tight.log"; fail "python tight: no warning"; }
[[ $(grep -c -- "--defsym=__sce_headroom=0x4000" "$T/py-tight.log") == 2 ]] || \
  { cat "$T/py-tight.log"; fail "python tight: the fix isn't named in both the warning and the error"; }
$PACK pack --elf "$T/tight_h.elf" --vpk "$T/py/tight_h.vpk" --title-id VPKT00001 --version 01.02 --name x \
  > "$T/py-tight_h.log" 2>&1 || { cat "$T/py-tight_h.log"; fail "python tight_h"; }
if grep -q warning "$T/py-tight_h.log"; then cat "$T/py-tight_h.log"; fail "python tight_h: a warning"; fi
$PACK check "$T/tight_h.elf" | grep "__sce_headroom 0x4000$" > /dev/null || fail "python check: no __sce_headroom 0x4000"
$PACK check "$T/good.elf" | grep "__sce_headroom not set$" > /dev/null || fail "python check good: headroom"
"$B/arm-vita-eabi-nm" "$T/tight_h.elf" | grep "^00004000 A __sce_headroom$" > /dev/null || fail "nm: tight_h has no __sce_headroom"
echo "ok: vpk_pack.py names the headroom fix for tight, packs it linked with __sce_headroom"

# CMake, with the art the Python run made
mkdir -p "$T/cm"
cp "$T/good.c" "$T/bad.c" "$T/lto.c" "$T/tight.c" "$T/cm/"
cat > "$T/cm/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
project(vpktest C)
include("$KIT/vita/cmake/VpkApp.cmake")
set(CMAKE_C_FLAGS "-O2")
set(sys "$T/py/sce_sys")
set(app \${APP})
if(app STREQUAL "lto")
  set(CMAKE_C_FLAGS "-O2 -flto")
endif()
set(src \${app})
set(extra "")
if(app MATCHES "^tight")
  set(src tight)
endif()
if(DEFINED HEADROOM_ARG)
  set(extra SCE_HEADROOM \${HEADROOM_ARG})
endif()
add_executable(\${app} \${src}.c)
target_include_directories(\${app} PRIVATE "$KIT/vita/runtime/procparam")
target_compile_definitions(\${app} PRIVATE PAD=$PAD)
target_link_options(\${app} PRIVATE -Wl,-q)
add_custom_target(art)
vpk_add_app(\${app} VPK "\${CMAKE_BINARY_DIR}/\${app}.vpk" TITLE_ID VPKT00001 VERSION 01.02 NAME "Kit Test"
  FILE "\${sys}/icon0.png" sce_sys/icon0.png
       "\${sys}/livearea/contents/bg.png" sce_sys/livearea/contents/bg.png
       "\${sys}/livearea/contents/startup.png" sce_sys/livearea/contents/startup.png
       "\${sys}/livearea/contents/template.xml" sce_sys/livearea/contents/template.xml
       "$T/good.c" src/good.c
  DEPENDS art \${extra})
EOF
for app in good bad lto; do
  cmake -S "$T/cm" -B "$T/cm/b-$app" -G Ninja -DAPP=$app \
    -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" > "$T/cm-$app.log" 2>&1 \
    || { cat "$T/cm-$app.log"; fail "cmake configure $app"; }
  if cmake --build "$T/cm/b-$app" >> "$T/cm-$app.log" 2>&1; then
    [[ $app == good ]] || fail "cmake packed $app"
  else
    [[ $app != good ]] || { cat "$T/cm-$app.log"; fail "cmake build good"; }
    grep -q "sceUserMainThreadStackSize" "$T/cm-$app.log" || { cat "$T/cm-$app.log"; fail "cmake $app: message"; }
    [[ ! -e "$T/cm/b-$app/$app.vpk" ]] || fail "cmake left $app.vpk"
  fi
done
grep -q "_newlib_heap_size_user" "$T/cm-bad.log" || fail "cmake bad: the weak heap reference passed"
$PACK show "$T/cm/b-good/good.vpk" > "$T/cm.show"
# the two eboot.bins come from separate links; everything else must match
if ! diff <(grep -v "^file eboot.bin " "$T/py.show") <(grep -v "^file eboot.bin " "$T/cm.show"); then
  fail "python and cmake VPKs differ"
fi
echo "ok: VpkApp.cmake packs good the same as vpk_pack.py, refuses bad and lto"

# CMake, tight: SCE_HEADROOM and VPK_SCE_HEADROOM reach the link line and the ELF; without either it fails
for app in tight tight_arg tight_var; do
  def=""
  [[ $app == tight_arg ]] && def=-DHEADROOM_ARG=0x4000
  [[ $app == tight_var ]] && def=-DVPK_SCE_HEADROOM=0x4000
  cmake -S "$T/cm" -B "$T/cm/b-$app" -G Ninja -DAPP=$app $def \
    -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" > "$T/cm-$app.log" 2>&1 \
    || { cat "$T/cm-$app.log"; fail "cmake configure $app"; }
  if cmake --build "$T/cm/b-$app" -- -v >> "$T/cm-$app.log" 2>&1; then
    [[ $app != tight ]] || fail "cmake packed tight without SCE_HEADROOM"
    grep -q -- "-Wl,--defsym=__sce_headroom=0x4000" "$T/cm-$app.log" || fail "cmake $app: no defsym on the link line"
    "$B/arm-vita-eabi-nm" "$T/cm/b-$app/$app" | grep "^00004000 A __sce_headroom$" > /dev/null || fail "cmake $app: no symbol"
    [[ -e "$T/cm/b-$app/$app.vpk" ]] || fail "cmake $app: no VPK"
  else
    [[ $app == tight ]] || { cat "$T/cm-$app.log"; fail "cmake build $app"; }
    grep -q "segment 1 overlaps" "$T/cm-$app.log" || { cat "$T/cm-$app.log"; fail "cmake tight: vita-elf-create's error"; }
  fi
done
if cmake -S "$T/cm" -B "$T/cm/b-badroom" -G Ninja -DAPP=tight_arg -DHEADROOM_ARG=16k \
    -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" > "$T/cm-badroom.log" 2>&1; then
  fail "cmake took SCE_HEADROOM 16k"
fi
grep -q "SCE_HEADROOM 16k is not a number" "$T/cm-badroom.log" || { cat "$T/cm-badroom.log"; fail "cmake badroom: message"; }
echo "ok: VpkApp.cmake's SCE_HEADROOM / VPK_SCE_HEADROOM link tight with __sce_headroom; without, it fails"
