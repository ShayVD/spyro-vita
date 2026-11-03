#!/bin/sh
# Native draw lists (decisions D82) against the OT walk's parse: a scenario
# with --fast-render 2 and nothing else (n0), and with --native-draw-check
# (chk: each converted renderer call also runs both ways and compares the
# triangles PsyCross's walk makes; the game goes on with the native draw
# lists), from the same binary. Every trace, shot, WAV and save file of the
# pair must be byte-identical, and the chk run's "native draw check" lines
# must say 0 differ. The host is deterministic (D38).
# Usage: [BIN=path/to/spyro] [DISC=disc/files] tools/native_draw_ab.sh PREFIX [SCENARIO...]
#   -> logs/PREFIX-SCENARIO-n0, logs/PREFIX-SCENARIO-chk, and a summary line per scenario.
# Scenarios (default: demo gems wsi vc lod routeA routeB): fast_render_ab.sh's, and
# vc (gems with the vertex cache, D39's hook; its "host vcache" line must find
# vertices, D89) and whole (the 100,000-vblank tour).
# MODE=n1 runs --native-draw in place of the check (timing, --prof-draw).
cd "$(dirname "$0")/.."
P=$1; shift
[ $# -gt 0 ] || set -- demo gems wsi vc lod routeA routeB
B=${BIN:-build/linux32/src/platform/linux32/spyro}; DISC=${DISC:-disc/files}; M=${MODE:-chk}
run() { # NAME MODE
  n=$1; m=$2; D=logs/$P-$n-$m
  rm -rf $D; mkdir -p $D
  extra=
  case $n in demo) extra="--wav $D/audio.wav";; routeA|routeB|whole) extra="--save $D/save";; esac
  case $m in n1) extra="$extra --native-draw";; chk) extra="$extra --native-draw-check";; esac
  GEMS="--frames 8768 --pad logs/pads/gems.txt --shot-every 500"
  case $n in
  gems) args="$GEMS --widescreen";;
  demo) args="--frames 12000 --shot-every 500";;
  wsi) args="--frames 12000 --shot-every 500 --widescreen --interp --out960";;
  lod) args="$GEMS --widescreen --draw-distance 2 --lod-stats 300";;
  vc) args="$GEMS --widescreen --vcache";;
  routeA) args="--frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels --tour-stay 300 --pad-after tools/oracle/pads/route_portal_exit.txt --shot-every 250";;
  routeB) args="--frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels,21 --tour-stay 300 --pad-after tools/oracle/pads/route_vortex_balloon.txt --shot-every 250";;
  whole) args="--frames 100000 --pad tools/oracle/pads/newgame_host.txt --pad-after tools/oracle/pads/fairy_after.txt --shot-every 1000 --tour 11,12,13,14,15,20,21,22,23,24,25,30,31,32,33,34,35,40,41,42,43,44,45,50,51,52,53,54,55,60,61,62,63,end2,64,end3,credits,levels,lives --tour-stay 300";;
  *) echo "unknown scenario $n"; exit 2;;
  esac
  tools/tc $B --disc $DISC --fast-render 2 --prof-draw --log $D/spyro.log --trace $D/trace.tsv \
    --shot-dir $D $extra $args > $D/stdout.txt 2>&1
  echo "exit $?" >> $D/stdout.txt
}
for n in "$@"; do
  run $n n0; run $n $M
  a=logs/$P-$n-n0; b=logs/$P-$n-$M; same=0; diff=0
  for x in $(cd $a && find . -type f ! -name spyro.log ! -name stdout.txt | sort); do
    if cmp -s "$a/$x" "$b/$x"; then same=$((same + 1)); else diff=$((diff + 1)); echo "  DIFF $n $x"; fi
  done
  [ "$(cd $b && find . -type f | wc -l)" = "$(cd $a && find . -type f | wc -l)" ] || echo "  $n: file lists differ"
  grep -h "native draw check: func" $b/spyro.log | sed 's/^/  /'
  # vc: the host's pictures don't use the cache, so the files can't show it
  # missing; the hand renderers must feed it (D89)
  if [ $n = vc ]; then
    grep -h "host vcache:" $a/stdout.txt $b/stdout.txt | sed 's/^/  /'
    grep -q "vertices found 0.0%" $a/stdout.txt $b/stdout.txt && echo "  vc: NO VERTICES FOUND (the cache isn't fed)"
  fi
  echo "$n: $same identical, $diff different ($(grep -h '^exit' $a/stdout.txt $b/stdout.txt | tr '\n' ' '))"
done
