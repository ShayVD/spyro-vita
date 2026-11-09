#!/bin/sh
# Stage 7e (decisions D125): the presenter's in-between frame against D30's
# pass. Each scenario runs with --interp (D30's pass draws the in-between
# frame: the reference, "i") and then in MODE:
#   chk  --pres-check: the presenter draws each in-between frame in play
#        too, and its list is compared with D30's record for record ("pres
#        check" lines must say 0 differ); D30's is the one shown, so every
#        file must be byte-identical to the reference's;
#   m2   --interp-mode 2: the presenter's frame is the one shown in play;
#        every trace, shot, WAV and card must be byte-identical to D30's.
# ND=1 adds --native-draw to both runs (the Vita's records, D91); FR=N adds
# --fast-render N (3, the default since D127, or 0).
# Usage: [BIN=...] [DISC=disc/files] [MODE=chk|m2] [ND=1] [FR=N] [I_FROM=PREFIX]
#        tools/pres_check_ab.sh PREFIX [SCENARIO...]
#   -> logs/PREFIX-SCENARIO-i, logs/PREFIX-SCENARIO-MODE, a summary line each.
# Scenarios (default: demo wsi gems routeA routeB): native_draw_ab.sh's, all
# with --interp: demo (the attract demos), wsi (the same at 16:9: the
# demos' 16:9 pass keeps its own visibility state), gems (16:9 play),
# routeA, routeB, whole (the 100,000-vblank tour).
# I_FROM=PREFIX: no reference run; compare with logs/PREFIX-SCENARIO-i.
# Each run has a stall guard: timeout ${STALL:-14400} s inside the container.
cd "$(dirname "$0")/.."
P=$1; shift
[ $# -gt 0 ] || set -- demo wsi gems routeA routeB
B=${BIN:-build/linux32/src/platform/linux32/spyro}; DISC=${DISC:-disc/files}; M=${MODE:-chk}
nd=; [ "${ND:-0}" = 1 ] && nd=--native-draw
[ -n "${FR:-}" ] && nd="$nd --fast-render $FR"
run() { # NAME MODE
  n=$1; m=$2; D=logs/$P-$n-$m
  rm -rf $D; mkdir -p $D
  extra="--save $D/save --interp $nd"
  case $n in demo|routeA|wsi) extra="$extra --wav $D/audio.wav";; esac
  case $m in chk) extra="$extra --pres-check";; m2) extra="$extra --interp-mode 2";; esac
  GEMS="--frames 8768 --pad logs/pads/gems.txt --shot-every 500"
  case $n in
  gems) args="$GEMS --widescreen";;
  demo) args="--frames 12000 --shot-every 500";;
  wsi) args="--frames 12000 --shot-every 500 --widescreen --out960";;
  routeA) args="--frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels --tour-stay 300 --pad-after tools/oracle/pads/route_portal_exit.txt --shot-every 250";;
  routeB) args="--frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels,21 --tour-stay 300 --pad-after tools/oracle/pads/route_vortex_balloon.txt --shot-every 250";;
  whole) args="--frames 100000 --pad tools/oracle/pads/newgame_host.txt --pad-after tools/oracle/pads/fairy_after.txt --shot-every 1000 --tour 11,12,13,14,15,20,21,22,23,24,25,30,31,32,33,34,35,40,41,42,43,44,45,50,51,52,53,54,55,60,61,62,63,end2,64,end3,credits,levels,lives --tour-stay 300";;
  *) echo "unknown scenario $n"; exit 2;;
  esac
  tools/tc timeout ${STALL:-14400} $B --disc $DISC --log $D/spyro.log --trace $D/trace.tsv \
    --shot-dir $D $extra $args > $D/stdout.txt 2>&1
  echo "exit $?" >> $D/stdout.txt
}
for n in "$@"; do
  if [ -n "${I_FROM:-}" ]; then a=logs/$I_FROM-$n-i; else run $n i; a=logs/$P-$n-i; fi
  run $n $M
  b=logs/$P-$n-$M; same=0; diff=0
  for x in $(cd $a && find . -type f ! -name spyro.log ! -name stdout.txt | sort); do
    if cmp -s "$a/$x" "$b/$x"; then same=$((same + 1)); else diff=$((diff + 1)); echo "  DIFF $n $x"; fi
  done
  [ "$(cd $b && find . -type f | wc -l)" = "$(cd $a && find . -type f | wc -l)" ] || echo "  $n: file lists differ"
  grep -h "pres check: [0-9]* frames" $b/spyro.log | sed 's/^.*pres check: /  /'
  grep -h "pres check: frame" $b/spyro.log | head -3 | sed 's/^.*pres check: /  /'
  echo "$n: $same identical, $diff different ($(grep -h '^exit' $a/stdout.txt $b/stdout.txt | tr '\n' ' '))"
done
