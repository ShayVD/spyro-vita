#!/bin/sh
# The hand-written renderers and inline GTE commands against the translation
# they replace (decisions D70): each scenario runs with --fast-render 0, then
# 1, from the same binary, and every trace, shot, WAV and save file of the
# pair must be byte-identical. The host is deterministic (D38).
# Usage: [BIN=path/to/spyro] [DISC=disc/files] tools/fast_render_ab.sh PREFIX [SCENARIO...]
#   -> logs/PREFIX-SCENARIO-f0, logs/PREFIX-SCENARIO-f1, and a summary line per scenario.
# Scenarios (default: gems demo wsi lod routeA routeB): gems (Artisans gem
# script, 16:9), demo (attract demos, 4:3, with audio), wsi (attract demos,
# 16:9 with 60 fps in-between frames at 960x544), lod (gems at 2x draw
# distance), routeA/routeB (host_checks.sh's portal/exit and vortex/balloon
# routes), whole (host_checks.sh's whole-game tour: 100,000 vblanks).
# Runs one at a time (Docker may have few CPUs); --prof-draw logs the
# renderers' CPU time in each spyro.log.
cd "$(dirname "$0")/.."
P=$1; shift
[ $# -gt 0 ] || set -- gems demo wsi lod routeA routeB
B=${BIN:-build/linux32/src/platform/linux32/spyro}; DISC=${DISC:-disc/files}
run() { # NAME FAST args...
  D=logs/$P-$1-f$2; n=$1; f=$2; shift 2
  rm -rf $D; mkdir -p $D
  extra=
  case $n in demo) extra="--wav $D/audio.wav";; routeA|routeB|whole) extra="--save $D/save";; esac
  tools/tc $B --disc $DISC --fast-render $f --prof-draw --log $D/spyro.log --trace $D/trace.tsv \
    --shot-dir $D $extra "$@" > $D/stdout.txt 2>&1
  echo "exit $?" >> $D/stdout.txt
}
scenario() { # NAME args...
  n=$1; shift
  for f in 0 1; do run $n $f "$@"; done
  a=logs/$P-$n-f0; b=logs/$P-$n-f1; same=0; diff=0
  for x in $(cd $a && find . -type f ! -name spyro.log ! -name stdout.txt | sort); do
    if cmp -s "$a/$x" "$b/$x"; then same=$((same + 1)); else diff=$((diff + 1)); echo "  DIFF $n $x"; fi
  done
  [ -d "$b" ] && [ "$(cd $b && find . -type f | wc -l)" = "$(cd $a && find . -type f | wc -l)" ] || echo "  $n: file lists differ"
  echo "$n: $same identical, $diff different ($(grep -h '^exit' $a/stdout.txt $b/stdout.txt | tr '\n' ' '))"
}
GEMS="--frames 8768 --pad logs/pads/gems.txt --shot-every 500"
for s in "$@"; do
  case $s in
  gems) scenario gems $GEMS --widescreen ;;
  demo) scenario demo --frames 12000 --shot-every 500 ;;
  wsi) scenario wsi --frames 12000 --shot-every 500 --widescreen --interp --out960 ;;
  lod) scenario lod $GEMS --widescreen --draw-distance 2 --lod-stats 300 ;;
  routeA) scenario routeA --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels --tour-stay 300 \
    --pad-after tools/oracle/pads/route_portal_exit.txt --shot-every 250 ;;
  routeB) scenario routeB --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels,21 --tour-stay 300 \
    --pad-after tools/oracle/pads/route_vortex_balloon.txt --shot-every 250 ;;
  whole) scenario whole --frames 100000 --pad tools/oracle/pads/newgame_host.txt --pad-after tools/oracle/pads/fairy_after.txt \
    --shot-every 1000 --tour 11,12,13,14,15,20,21,22,23,24,25,30,31,32,33,34,35,40,41,42,43,44,45,50,51,52,53,54,55,60,61,62,63,end2,64,end3,credits,levels,lives --tour-stay 300 ;;
  *) echo "unknown scenario $s"; exit 2 ;;
  esac
done
