#!/bin/sh
# Differential fuzzing of the hand-written collision against its mips2c
# unit (src/game/collision_hand.c SPYRO_COL_FUZZ, decisions D83): every
# capture set's collision samples, each with N variants of its arguments,
# through a build with the units linked (cmake -DSPYRO_M2C_REF=ON).
# Usage: tools/oracle/col_fuzz.sh OUTDIR [N] [BUILDDIR]
# OUTDIR/ALL.txt: per set and function, samples passed and failed and the
# number of "colshadow" mismatch lines (0 when the two agree).
cd "$(dirname "$0")/../.."
O=$1; N=${2:-50}; B=${3:-build/linux32-ref}; BIN=$B/src/platform/linux32/spyro
mkdir -p $O
for c in $(ls logs/caps); do
  for f in ColTraceRay ColSphere ColFindFloor func_8004DF24 ColSpyroInHitbox ColMoby; do
    ls logs/caps/$c | grep -q "^${f}_" || continue
    tools/tc env SPYRO_COL_FUZZ=$N $BIN --replay logs/caps/$c --only $f --ignore tools/oracle/ignore_ranges.txt \
      --fnmap $B/oracle/fnmap.txt --disc disc/files > $O/$c-$f.txt 2>&1
    echo "$c $f: $(grep -c ' PASS ' $O/$c-$f.txt) pass $(grep -c ' FAIL ' $O/$c-$f.txt) fail $(grep -c colshadow $O/$c-$f.txt) mismatch-lines" >> $O/ALL.txt
  done
done
echo done >> $O/ALL.txt
