#!/bin/sh
# The native SPU mix against libspu.c's (decisions D84): each scenario runs
# with --mixer 1, then 3, from the same binary, with --wav, and every WAV,
# trace, shot and save file of the pair must be byte-identical. The host is
# deterministic (D38) and the mix runs in virtual time.
# Usage: [BIN=path/to/spyro] [DISC=disc/files] [MIX="A B"] tools/mixer_ab.sh PREFIX [SCENARIO...]
#   -> logs/PREFIX-SCENARIO-mA, logs/PREFIX-SCENARIO-mB, and a summary line per scenario.
# Scenarios (default: demo gems routeA routeB): demo (attract demos, 12,000
# vblanks), gems (Artisans gem script, 16:9), routeA/routeB (host_checks.sh's
# portal/exit and vortex/balloon routes), state (gems with a save state saved
# at 3000 and loaded at 6000: the PCM cache after a load), whole
# (host_checks.sh's whole-game tour: 100,000 vblanks, a 294 MB WAV each).
# Up to JOBS (2) runs at a time.
cd "$(dirname "$0")/.."
P=$1; shift
[ $# -gt 0 ] || set -- demo gems routeA routeB
B=${BIN:-build/linux32/src/platform/linux32/spyro}; DISC=${DISC:-disc/files}
set -f; MIX=${MIX:-1 3}; MA=${MIX% *}; MB=${MIX#* }; set +f
run() { # NAME MIXER args...
  D=logs/$P-$1-m$2; n=$1; m=$2; shift 2
  rm -rf $D; mkdir -p $D
  extra=
  # each run its own memory cards: the pair runs at once, and two runs sharing
  # the default card folder diverge at the title's card check (D88)
  case $n in routeA|routeB|whole|state|gems) extra="--save $D/save";; esac
  tools/tc $B --disc $DISC --mixer $m --log $D/spyro.log --trace $D/trace.tsv --wav $D/audio.wav \
    --shot-dir $D $extra "$@" > $D/stdout.txt 2>&1
  echo "exit $?" >> $D/stdout.txt
}
scenario() { # NAME args...
  n=$1; shift
  run $n $MA "$@" & run $n $MB "$@" & wait
  a=logs/$P-$n-m$MA; b=logs/$P-$n-m$MB; same=0; diff=0
  for x in $(cd $a && find . -type f ! -name spyro.log ! -name stdout.txt | sort); do
    if cmp -s "$a/$x" "$b/$x"; then same=$((same + 1)); else diff=$((diff + 1)); echo "  DIFF $n $x"; fi
  done
  [ -d "$b" ] && [ "$(cd $b && find . -type f | wc -l)" = "$(cd $a && find . -type f | wc -l)" ] || echo "  $n: file lists differ"
  echo "$n: $same identical, $diff different; audio.wav $(shasum $b/audio.wav | cut -c1-12), $(stat -f %z $b/audio.wav 2>/dev/null || stat -c %s $b/audio.wav) bytes ($(grep -h '^exit' $a/stdout.txt $b/stdout.txt | tr '\n' ' '))"
}
GEMS="--frames 8768 --pad logs/pads/gems.txt --shot-every 500"
for s in "$@"; do
  case $s in
  demo) scenario demo --frames 12000 --shot-every 500 ;;
  gems) scenario gems $GEMS --widescreen ;;
  state) scenario state $GEMS --state 3000:save:1 --state 6000:load:1 ;;
  routeA) scenario routeA --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels --tour-stay 300 \
    --pad-after tools/oracle/pads/route_portal_exit.txt --shot-every 250 ;;
  routeB) scenario routeB --frames 13000 --pad tools/oracle/pads/newgame_host.txt --tour levels,21 --tour-stay 300 \
    --pad-after tools/oracle/pads/route_vortex_balloon.txt --shot-every 250 ;;
  whole) scenario whole --frames 100000 --pad tools/oracle/pads/newgame_host.txt --pad-after tools/oracle/pads/fairy_after.txt \
    --shot-every 1000 --tour 11,12,13,14,15,20,21,22,23,24,25,30,31,32,33,34,35,40,41,42,43,44,45,50,51,52,53,54,55,60,61,62,63,end2,64,end3,credits,levels,lives --tour-stay 300 ;;
  *) echo "unknown scenario $s"; exit 2 ;;
  esac
done
