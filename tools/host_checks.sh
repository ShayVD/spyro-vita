#!/bin/sh
# Host determinism checks for the hand-C merge: attract demos, the whole-game
# tour (PR #7, logs/run21-host-whole's command), routes A/B (PR #11) and the
# vertex cache's attract demo (#281: D of docs/host-build.md).
# Usage: [BIN=path/to/spyro] [DISC=disc/image] tools/host_checks.sh PREFIX -> logs/PREFIX-{demo,whole,routeA,routeB,vcache}
# Compare two prefixes with cmp: the host is deterministic (decisions D38).
# The vcache run's pictures don't use the cache's answers (the software
# renderer), so compare its vcache.txt: the cache's counters (the offset
# model's HAND_CX missed only there, #281).
# DISC: the extracted files (default disc/files) or a folder with the disc image (D60).
# EXTRA: more options for every run (EXTRA=--walk-lazy: the OT walk from a copy, D71).
cd "$(dirname "$0")/.."
P=$1; B=${BIN:-build/linux32/src/platform/linux32/spyro}; DISC=${DISC:-disc/files}
# linux64's binary (D174) runs under qemu-x86_64, as linux32's under qemu-i386 (binfmt)
# macOS's (D250 O4) runs on the Mac itself, not in tools/tc's container
TC=tools/tc
case "$(file -b "$B" 2>/dev/null)" in *Mach-O*) TC= ;; *x86-64*) EMU=${EMU-qemu-x86_64} ;; esac
run() { D=logs/$P-$1; shift; mkdir -p $D; $TC $EMU $B --disc $DISC --log $D/spyro.log --trace $D/trace.tsv --shot-dir $D $EXTRA "$@" > $D/stdout.txt 2>&1; echo "exit $?" >> $D/spyro.log; }
run demo --frames 12000 --shot-every 500 --wav logs/$P-demo/audio.wav --save logs/$P-demo/save &
run whole --frames 100000 --pad tools/oracle/pads/newgame_host.txt --pad-after tools/oracle/pads/fairy_after.txt --save logs/$P-whole/save \
  --shot-every 1000 --tour 11,12,13,14,15,20,21,22,23,24,25,30,31,32,33,34,35,40,41,42,43,44,45,50,51,52,53,54,55,60,61,62,63,end2,64,end3,credits,levels,lives --tour-stay 300 &
run routeA --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels --tour-stay 300 --pad-after tools/oracle/pads/route_portal_exit.txt --save logs/$P-routeA/save --shot-every 250 --wav logs/$P-routeA/audio.wav &
run routeB --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels,21 --tour-stay 300 --pad-after tools/oracle/pads/route_vortex_balloon.txt --save logs/$P-routeB/save --shot-every 250 &
run vcache --frames 7000 --draw-distance 2 --interp --vcache --widescreen --save logs/$P-vcache/save &
wait
# its counters without the log's vblank stamps: linux64, linuxa64 and macos
# give the same file; linux32's x87 floats differ in four lines (the cost
# model's, `repeats` and `resolved`'s first count: D267)
grep vcache logs/$P-vcache/spyro.log | sed 's/^\[ *[0-9]*\] //' > logs/$P-vcache/vcache.txt
