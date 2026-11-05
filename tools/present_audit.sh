#!/bin/sh
# Stage 7a's audits (D104): tools/host_checks.sh's four runs with --interp,
# the per-gamestate audit and the read trace (every 30th in-between pass, the
# tour's every 300th). Slow under qemu: the read trace faults on every access
# to the game's globals. Summarise with tools/present_reads.py.
# Usage: [BIN=path/to/spyro] tools/present_audit.sh PREFIX -> logs/PREFIX-{demo,whole,routeA,routeB}
# Each run's trace and memory card must stay byte-identical to host_checks.sh's.
cd "$(dirname "$0")/.."
P=$1; B=${BIN:-build/linux32/src/platform/linux32/spyro}; DISC=${DISC:-disc/files}
run() { D=logs/$P-$1; E=$2; shift 2; mkdir -p $D; tools/tc $B --disc $DISC --log $D/spyro.log --trace $D/trace.tsv --shot-dir $D --interp --present-audit --present-reads $D/reads.tsv --present-reads-every $E "$@" > $D/stdout.txt 2>&1; echo "exit $?" >> $D/spyro.log; }
run demo 30 --frames 12000 &
run whole 300 --frames 100000 --pad tools/oracle/pads/newgame_host.txt --pad-after tools/oracle/pads/fairy_after.txt --save logs/$P-whole/save \
  --tour 11,12,13,14,15,20,21,22,23,24,25,30,31,32,33,34,35,40,41,42,43,44,45,50,51,52,53,54,55,60,61,62,63,end2,64,end3,credits,levels,lives --tour-stay 300 &
run routeA 30 --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels --tour-stay 300 --pad-after tools/oracle/pads/route_portal_exit.txt --save logs/$P-routeA/save &
run routeB 30 --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels,21 --tour-stay 300 --pad-after tools/oracle/pads/route_vortex_balloon.txt --save logs/$P-routeB/save &
wait
