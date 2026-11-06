#!/usr/bin/env bash
# Stage 8e (docs/native-plan.md, "The stage 8 plan"; D118): the native and
# Vita builds link no PsyCross code. Two checks on a build directory:
#   1. the game's link map (tools/native_audit_map.sh) loads no PsyCross
#      library (libpsycross.a, libpsycross_ref.a) and takes no member of one;
#   2. nm of the game binary: no symbol of the reference library
#      (libpsycross_ref.a, when the build has it): neither its psyx_* renames
#      nor the names it keeps (GTE_operator, ...) unless our own objects define
#      them too. PsyCross's headers (option A, D118) define no symbol of their
#      own beyond static inlines, which have no external name.
# The arena build links psycross_ref on purpose (its mips2c units, the walk
# checks): run this on native-memory builds only.
#   Vita, in the VitaSDK image:
#     docker run --rm -v "$PWD:/work" -w /work vitasdk/vitasdk:latest tools/check_no_psycross.sh build/vita vita
#   host (linux32, native memory):
#     tools/tc tools/check_no_psycross.sh build/linux32-native linux32
# Exit status 0 when both hold; prints what it found either way.
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:?usage: tools/check_no_psycross.sh BUILD_DIR [vita|linux32]}"
host="${2:-vita}"
case "$host" in
  vita)
    : "${VITASDK:=/usr/local/vitasdk}"
    export PATH="$VITASDK/bin:$PATH"
    bin="$out/src/platform/vita/spyro_vita" map="$out/spyro_vita.map" nm=arm-vita-eabi-nm ;;
  linux32) bin="$out/src/platform/linux32/spyro" map="$out/spyro.map" nm=i686-linux-gnu-nm ;;
  *) echo "check_no_psycross: host is vita or linux32" >&2; exit 2 ;;
esac
[ -f "$bin" ] || { echo "check_no_psycross: no $bin (build first)" >&2; exit 2; }
tools/native_audit_map.sh "$out" "$host" > /dev/null
fail=0

# 1. The link map
hits=$(grep -nE 'libpsycross(_ref)?\.a|CMakeFiles/psycross(_ref)?\.dir' "$map" || true)
if [ -n "$hits" ]; then
  echo "FAIL map: $map names a PsyCross library or object:"
  echo "$hits" | head -20
  fail=1
else
  echo "ok   map: $map loads no PsyCross library and no member of one"
fi

# 2. nm of the binary against the reference library's symbols
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$nm" --defined-only "$bin" | awk 'NF == 3 { print $3 }' | sort -u > "$tmp/bin"
ref=$(find "$out" -name 'libpsycross_ref.a' -not -path '*/tests/*' | head -1)
if [ -z "$ref" ]; then
  echo "skip nm:  $out has no libpsycross_ref.a to list PsyCross's symbols from (SPYRO_PSYCROSS_REF=OFF)"
else
  "$nm" -g --defined-only "$ref" 2> /dev/null | awk 'NF == 3 { print $3 }' | sort -u > "$tmp/ref"
  # The names our own objects define too (libgte's six functions, the GPU
  # state, the walk's entry points...): the binary has ours, which the map
  # check above already places.
  find "$out" \( -name '*.a' -o -name '*.o' -o -name '*.obj' \) \
       -not -name 'libpsycross*' -not -path '*CMakeFiles/psycross*' \
       -not -path '*/tests/*' -print0 |
    xargs -0 "$nm" -g --defined-only 2> /dev/null | awk 'NF == 3 { print $3 }' | sort -u > "$tmp/ours"
  { grep -E '^_?psyx_' "$tmp/ref" || true; comm -23 "$tmp/ref" "$tmp/ours" | grep -vE '^_?psyx_' || true; } |
    sort -u > "$tmp/theirs"
  found=$(comm -12 "$tmp/theirs" "$tmp/bin")
  if [ -n "$found" ]; then
    echo "FAIL nm:  $bin defines $(echo "$found" | wc -l | tr -d ' ') of the reference library's symbols:"
    echo "$found" | head -20
    fail=1
  else
    echo "ok   nm:  $bin defines none of the $(wc -l < "$tmp/theirs" | tr -d ' ') symbols only PsyCross's reference library defines"
  fi
fi
exit "$fail"
