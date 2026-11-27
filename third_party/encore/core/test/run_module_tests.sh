#!/usr/bin/env bash
# The portable modules' tests (ring, audio_ring, discimage with its mutants, menu, config) with any compiler: the
# host's (core/test/run_tests.sh), the PC's for Linux and for Windows (targets/pc/test/run_tests.sh, under wine),
# or a cross compiler with a runner. The Vita builds the same test files into an app of its own
# (targets/vita/encore/build_modules.sh).
#   core/test/run_module_tests.sh [extra cflags]
#   CC=x86_64-w64-mingw32-clang EXE=.exe RUN=wine core/test/run_module_tests.sh
# CC (default cc), RUN (a runner: qemu-arm, wine), EXE (the programs' suffix). OUT keeps the programs in that
# folder (NAME_test, discimage_test_mutantN) and NORUN=1 only builds them, for a run elsewhere (CI's pc-windows job).
# Otherwise it writes only into a temporary directory.
set -euo pipefail
k=$(cd "$(dirname "$0")/../.." && pwd)
cc=${CC:-cc}
run=(${RUN:-})
x=${EXE:-}
w=$(mktemp -d "${TMPDIR:-/tmp}/enc_modules.XXXXXX")
trap 'rm -rf "$w"' EXIT
o=${OUT:-$w}
mkdir -p "$o"
norun=${NORUN:-0}
flags=(-std=gnu99 -O1 -Wall -Wextra -Werror -I"$k/core/include" "$@")
c=$k/core/src t=$k/core/test
r() { [ "$norun" = 1 ] || ${run[@]+"${run[@]}"} "$@"; }
$cc "${flags[@]}" "$c/enc_ring.c" "$t/ring_test.c" -o "$o/ring_test$x"
r "$o/ring_test$x"
$cc "${flags[@]}" "$c/enc_audio_ring.c" "$t/audio_ring_test.c" -lm -o "$o/audio_ring_test$x"
r "$o/audio_ring_test$x" | tail -n 1
mkdir -p "$w/disc" "$w/menu" "$w/cfg"
$cc "${flags[@]}" "$c/enc_discimage.c" "$t/discimage_test.c" -o "$o/discimage_test$x"
r "$o/discimage_test$x" "$w/disc"
for m in 1 2; do # 1: no cue-first pass, 2: a cue's first track even if it is audio
  $cc "${flags[@]}" -DDISCIMAGE_MUTANT=$m "$c/enc_discimage.c" "$t/discimage_test.c" -o "$o/discimage_test_mutant$m$x"
  [ "$norun" = 1 ] && continue
  if r "$o/discimage_test_mutant$m$x" "$w/disc" > /dev/null; then echo "discimage_test: mutant $m passed"; exit 1
  else echo "discimage_test: mutant $m fails, as it must"; fi
done
$cc "${flags[@]}" "$c/enc_menu.c" "$c/enc_menu_panel.c" "$t/menu_test.c" -o "$o/menu_test$x"
r "$o/menu_test$x" "$w/menu"
$cc "${flags[@]}" "$c/enc_config.c" "$t/config_test.c" -o "$o/config_test$x"
r "$o/config_test$x" "$w/cfg"
[ "$norun" = 1 ] && echo "modules: built in $o, not run"
exit 0
