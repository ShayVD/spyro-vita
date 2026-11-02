#!/usr/bin/env bash
# Relink a build's game binary with a link map, for tools/native_audit.py
# (docs/native-plan.md, decisions D76). The build itself isn't touched: the
# same link command runs again with -Wl,-Map, writes NAME.audit next to the
# real binary, and is removed after its symbols are listed.
#   Vita, in the VitaSDK image, after tools/ci/build_vpk.sh:
#     docker run --rm -v "$PWD:/work" -w /work vitasdk/vitasdk:latest tools/native_audit_map.sh build/vita-audit
#   host (linux32), for a profile (docs/native-plan.md "Run time"):
#     tools/tc tools/native_audit_map.sh build/linux32-prof linux32
# Leaves BUILD_DIR/<name>.map and BUILD_DIR/<name>.syms (nm -S).
set -euo pipefail
cd "$(dirname "$0")/.."
out="${1:-build/vita}"
host="${2:-vita}"
case "$host" in
  vita)
    : "${VITASDK:=/usr/local/vitasdk}"
    export PATH="$VITASDK/bin:$PATH"
    target=src/platform/vita/spyro_vita nm=arm-vita-eabi-nm ;;
  linux32) target=src/platform/linux32/spyro nm=i686-linux-gnu-nm ;;
  *) echo "native_audit_map: host is vita or linux32" >&2; exit 1 ;;
esac
name=$(basename "$target")
cd "$out"
cmd=$(ninja -t commands "$target" | tail -1)
case "$cmd" in
  *"-o $target "*) ;;
  *) echo "native_audit_map: unexpected link command" >&2; exit 1 ;;
esac
cmd=${cmd/"-o $target "/"-o $target.audit -Wl,-Map=$name.map "}
eval "$cmd"
# Symbol addresses and sizes: profiles name functions, the map places them
"$nm" -S -C --defined-only "$target.audit" > "$name.syms"
rm -f "$target.audit"
echo "map: $out/$name.map, symbols: $out/$name.syms"
