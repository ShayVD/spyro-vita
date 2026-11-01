#!/bin/sh
# Host determinism checks for the hand-C merge: attract demos, the whole-game
# tour (PR #7, logs/run21-host-whole's command) and routes A/B (PR #11).
# Usage: [BIN=path/to/spyro] [DISC=disc/image] tools/host_checks.sh PREFIX -> logs/PREFIX-{demo,whole,routeA,routeB}
# Compare two prefixes with cmp: the host is deterministic (decisions D38).
# DISC: the extracted files (default disc/files) or a folder with the disc image (D60).
# EXTRA: more options for every run (EXTRA=--walk-lazy: the OT walk from a copy, D71).
cd "$(dirname "$0")/.."
P=$1; B=${BIN:-build/linux32/src/platform/linux32/spyro}; DISC=${DISC:-disc/files}
run() { D=logs/$P-$1; shift; mkdir -p $D; tools/tc $B --disc $DISC --log $D/spyro.log --trace $D/trace.tsv --shot-dir $D $EXTRA "$@" > $D/stdout.txt 2>&1; echo "exit $?" >> $D/spyro.log; }
run demo --frames 12000 --shot-every 500 --wav logs/$P-demo/audio.wav &
run whole --frames 100000 --pad tools/oracle/pads/newgame_host.txt --pad-after tools/oracle/pads/fairy_after.txt --save logs/$P-whole/save \
  --shot-every 1000 --tour 11,12,13,14,15,20,21,22,23,24,25,30,31,32,33,34,35,40,41,42,43,44,45,50,51,52,53,54,55,60,61,62,63,end2,64,end3,credits,levels,lives --tour-stay 300 &
run routeA --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels --tour-stay 300 --pad-after tools/oracle/pads/route_portal_exit.txt --save logs/$P-routeA/save --shot-every 250 --wav logs/$P-routeA/audio.wav &
run routeB --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels,21 --tour-stay 300 --pad-after tools/oracle/pads/route_vortex_balloon.txt --save logs/$P-routeB/save --shot-every 250 &
wait
