#!/usr/bin/env bash
# Run the linux32 host under qemu-i386's gdb stub and print a backtrace when
# it stops (crash, abort or breakpoint). Run inside tools/tc:
#   tools/tc tools/gdb_run.sh "<gdb commands; separated>" -- spyro args...
set -euo pipefail
cmds="$1"; shift; [ "$1" = "--" ] && shift
bin=build/linux32/src/platform/linux32/spyro
qemu-i386 -g 1234 "$bin" "$@" > /tmp/gdb_run.out 2>&1 &
sleep 1
args=(-batch -ex "set pagination off" -ex "set sysroot /usr/i686-linux-gnu" -ex "file $bin"
      -ex "target remote :1234")
IFS=';' read -ra extra <<< "$cmds"
for c in "${extra[@]}"; do args+=(-ex "$c"); done
args+=(-ex "continue" -ex "bt 25" -ex "info registers" -ex "kill")
gdb-multiarch "${args[@]}" 2>&1 | grep -v "^warning: \|Reading symbols\|^\[Inferior"
tail -5 /tmp/gdb_run.out
