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
# MODE=p2 runs native_draw=2 with its parse check (D92: every packet the walk
# parses with the records' converter is parsed by PsyCross too, "native draw
# parse check" lines); MODE=chkp the per-call check with native_draw=1 and the
# parse check of the other producers' packets.
# MODE=dir runs native_draw=1 with direct vertices and their check (D99: every
# packet the walk parses itself both ways, "native draw direct check" lines);
# MODE=dir2 the same with native_draw=2.
# MODE=wc0, wc1, wc1d, wc2, wc2d: the walk check (D117: every packet our walk
# parses also through PsyCross's walk, "walk check" lines; the reference
# build) with native_draw 0, 1, 1 and nd_direct, 2, 2 and nd_direct. The vc
# scenario is the walk check with the vertex cache on (no direct vertices
# there, D99).
# MODE=rc runs native_draw=1 with the presenter builds' check (stage 7d,
# D123: each renderer call also runs NAME_pres in its own context, "render
# ctx:" lines, which must say 0 differences and 0 writes); MODE=rc0 the same
# check with native_draw 0.
# MODE=frc: the renderers' per-call check (--fast-render-check; at FR=3 each
# call against fast_render=1's build, D124); MODE=poison: --render-poison
# (D124: after each renderer call what fast_render=3 doesn't write is
# overwritten); MODE=plain: nothing more (with FR=3, fast_render=3 alone);
# MODE=all3: native_draw=1 with the per-call check, the presenter check (at
# FR=3 chained: each call's readable run is the presenter check) and the
# walk check in one run.
# FR=N: the second run of each pair at --fast-render N (default 2; the n0
# run stays at 2, main's default).
# ND_ARGS goes to the second run of each pair only (D91: "--nd-var 2", "--nd-ab").
# N0_FROM=PREFIX: no n0 run; compare with logs/PREFIX-SCENARIO-n0 (one
# baseline for several modes).
cd "$(dirname "$0")/.."
P=$1; shift
[ $# -gt 0 ] || set -- demo gems wsi vc lod routeA routeB
B=${BIN:-build/linux32/src/platform/linux32/spyro}; DISC=${DISC:-disc/files}; M=${MODE:-chk}; FR=${FR:-2}
run() { # NAME MODE
  n=$1; m=$2; D=logs/$P-$n-$m
  rm -rf $D; mkdir -p $D
  extra=
  # every run its own card folder: without --save both runs of a pair share ./save,
  # and the second finds the card the first wrote (D91)
  extra="--save $D/save"
  case $n in demo) extra="$extra --wav $D/audio.wav";; esac
  case $m in n1) extra="$extra --native-draw ${ND_ARGS:-}";; chk) extra="$extra --native-draw-check ${ND_ARGS:-}";;
  p2) extra="$extra --native-draw-parse-check ${ND_ARGS:-}";;
  chkp) extra="$extra --native-draw-check --native-draw-parse-check ${ND_ARGS:-}";;
  dir) extra="$extra --native-draw --nd-direct --nd-direct-check ${ND_ARGS:-}";;
  dir2) extra="$extra --native-draw-parse --nd-direct --nd-direct-check ${ND_ARGS:-}";;
  wc0) extra="$extra --walk-check ${ND_ARGS:-}";;
  rc) extra="$extra --native-draw --render-ctx-check ${ND_ARGS:-}";;
  rc0) extra="$extra --render-ctx-check ${ND_ARGS:-}";;
  frc) extra="$extra --native-draw --fast-render-check ${ND_ARGS:-}";;
  frc0) extra="$extra --fast-render-check ${ND_ARGS:-}";;
  poison) extra="$extra --render-poison ${ND_ARGS:-}";;
  all3) extra="$extra --native-draw --fast-render-check --render-ctx-check --walk-check ${ND_ARGS:-}";;
  plain) extra="$extra ${ND_ARGS:-}";;
  wc1) extra="$extra --native-draw --walk-check ${ND_ARGS:-}";;
  wc1d) extra="$extra --native-draw --nd-direct --walk-check ${ND_ARGS:-}";;
  wc2) extra="$extra --native-draw-parse --walk-check ${ND_ARGS:-}";;
  wc2d) extra="$extra --native-draw-parse --nd-direct --walk-check ${ND_ARGS:-}";; esac
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
  fr=2
  [ "$m" = n0 ] || fr=$FR
  tools/tc $B --disc $DISC --fast-render $fr --prof-draw --log $D/spyro.log --trace $D/trace.tsv \
    --shot-dir $D $extra $args > $D/stdout.txt 2>&1
  echo "exit $?" >> $D/stdout.txt
}
for n in "$@"; do
  if [ -n "${N0_FROM:-}" ]; then a=logs/$N0_FROM-$n-n0; else run $n n0; a=logs/$P-$n-n0; fi
  run $n $M
  b=logs/$P-$n-$M; same=0; diff=0
  for x in $(cd $a && find . -type f ! -name spyro.log ! -name stdout.txt | sort); do
    if cmp -s "$a/$x" "$b/$x"; then same=$((same + 1)); else diff=$((diff + 1)); echo "  DIFF $n $x"; fi
  done
  [ "$(cd $b && find . -type f | wc -l)" = "$(cd $a && find . -type f | wc -l)" ] || echo "  $n: file lists differ"
  grep -h "native draw check: func" $b/spyro.log | sed 's/^/  /'
  grep -h "native draw parse check: [0-9]" $b/spyro.log | sed 's/^/  /'
  grep -h "native draw direct check: [0-9]" $b/spyro.log | sed 's/^/  /'
  grep -h "walk check: [0-9]" $b/spyro.log | sed 's/^/  /'
  grep -h "render ctx: func[0-9A-F_]*: " $b/spyro.log | sed 's/^.*render ctx: /  /'
  grep -h "hand check: func[0-9A-F_]* [0-9]* calls" $b/spyro.log | sed 's/^.*hand check: /  /'
  grep -h "render poison:" $b/spyro.log | sed 's/^.*render poison: /  poison: /'
  grep -c "differ (" $b/spyro.log | sed 's/^/  lines naming a difference: /'
  # vc: the host's pictures don't use the cache, so the files can't show it
  # missing; the hand renderers must feed it (D89)
  if [ $n = vc ]; then
    grep -h "host vcache:" $a/stdout.txt $b/stdout.txt | sed 's/^/  /'
    grep -q "vertices found 0.0%" $a/stdout.txt $b/stdout.txt && echo "  vc: NO VERTICES FOUND (the cache isn't fed)"
  fi
  echo "$n: $same identical, $diff different ($(grep -h '^exit' $a/stdout.txt $b/stdout.txt | tr '\n' ' '))"
done
