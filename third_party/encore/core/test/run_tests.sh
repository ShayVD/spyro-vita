#!/usr/bin/env bash
# core/: the platform interface's conformance run, the host backend's own checks and the portable modules' tests
# (ring, audio_ring, discimage, menu, config), with CC (default cc), RUN (a
# runner such as qemu-arm) and extra flags:
#   core/test/run_tests.sh [-m32]
#   CC=arm-linux-gnueabihf-gcc RUN="qemu-arm -L /usr/arm-linux-gnueabihf" core/test/run_tests.sh
# The run writes only into a temporary ENC_HOST_ROOT.
set -euo pipefail
k=$(cd "$(dirname "$0")/../.." && pwd)
cc=${CC:-cc}
run=(${RUN:-})
o=$(mktemp -d "${TMPDIR:-/tmp}/enc_tests.XXXXXX")
trap 'rm -rf "$o"' EXIT
flags=(-std=gnu99 -O1 -Wall -Wextra -Werror "$@")
inc=(-I"$k/core/include" -I"$k/core/host" -I"$k/core/test")
src=("$k/core/src/enc_common.c" "$k/core/src/enc_blit.c" "$k/core/src/enc_disc.c" "$k/core/src/enc_discimage.c"
     "$k/core/src/enc_co.c" "$k/core/src/enc_event.c" "$k/core/host/enc_host.c" "$k/core/test/conformance.c")
$cc "${flags[@]}" "${inc[@]}" "${src[@]}" "$k/core/test/host_test.c" -lpthread -o "$o/host_test"
ENC_HOST_ROOT="$o/root" ${run[@]+"${run[@]}"} "$o/host_test" 2> "$o/log" > "$o/out" || { cat "$o/log" "$o/out"; exit 1; }
grep -c '  ok ' "$o/log" | sed 's/^/conformance: /; s/$/ checks ok/'
tail -n 1 "$o/out"
# the conformance file alone, as a target's test app builds it
$cc "${flags[@]}" -DENC_CONFORMANCE_MAIN "${inc[@]}" "${src[@]}" -lpthread -o "$o/conformance"
ENC_HOST_ROOT="$o/root2" ${run[@]+"${run[@]}"} "$o/conformance" 2> "$o/log2" || { cat "$o/log2"; exit 1; }
echo "conformance (standalone): ok"
# the portable modules (targets/vita/runtime/test/run_host_tests.sh runs the same tests through the old vpk_ paths)
"$k/core/test/run_module_tests.sh" "$@"
