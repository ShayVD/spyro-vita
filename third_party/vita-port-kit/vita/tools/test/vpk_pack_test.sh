#!/usr/bin/env bash
# vpk_pack.py and vita/cmake/VpkApp.cmake on three tiny apps (needs vitasdk, cmake and ninja; no game data):
#   good  VPK_PROCESS_PARAMS: both pack, and give the same param.sfo values and file list
#   bad   no process parameters at all (newlib still leaves an undefined weak _newlib_heap_size_user): both refuse
#   lto   the two variables without `used`, -flto: the stack size is dropped, both refuse
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
for app in good bad lto; do
  lto=""; [[ $app == lto ]] && lto=-flto
  "$B/arm-vita-eabi-gcc" -O2 $lto -I"$KIT/vita/runtime/procparam" -Wl,-q "$T/$app.c" -o "$T/$app.elf"
done

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

# CMake, with the art the Python run made
mkdir -p "$T/cm"
cp "$T/good.c" "$T/bad.c" "$T/lto.c" "$T/cm/"
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
add_executable(\${app} \${app}.c)
target_include_directories(\${app} PRIVATE "$KIT/vita/runtime/procparam")
target_link_options(\${app} PRIVATE -Wl,-q)
add_custom_target(art)
vpk_add_app(\${app} VPK "\${CMAKE_BINARY_DIR}/\${app}.vpk" TITLE_ID VPKT00001 VERSION 01.02 NAME "Kit Test"
  FILE "\${sys}/icon0.png" sce_sys/icon0.png
       "\${sys}/livearea/contents/bg.png" sce_sys/livearea/contents/bg.png
       "\${sys}/livearea/contents/startup.png" sce_sys/livearea/contents/startup.png
       "\${sys}/livearea/contents/template.xml" sce_sys/livearea/contents/template.xml
       "$T/good.c" src/good.c
  DEPENDS art)
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
