#!/bin/sh
# Replay every capture set in logs/caps through a build and summarise
# (tools/oracle/replay.sh does one set with build/linux32). Default build:
# build/linux32-ref, which links the mips2c units of the hand-written C
# (cmake -DSPYRO_M2C_REF=ON), so the collision shadow is on (decisions D83).
# Usage: tools/oracle/replay_all.sh OUTDIR [BUILDDIR] -> OUTDIR/SET.txt,
# SET.summary.txt and ALL.txt (each set's totals line).
cd "$(dirname "$0")/../.."
O=$1; B=${2:-build/linux32-ref}; BIN=$B/src/platform/linux32/spyro
mkdir -p $O $B/oracle
tools/tc sh -c "i686-linux-gnu-nm $BIN > $B/oracle/nm.txt"
python3 tools/oracle/fnmap.py build/s1-matching/build/psx.map $B/oracle/nm.txt > $B/oracle/fnmap.txt
for c in $(ls logs/caps); do
  tools/tc $BIN --replay logs/caps/$c --ignore tools/oracle/ignore_ranges.txt --fnmap $B/oracle/fnmap.txt \
    --disc disc/files > $O/$c.txt 2>&1
  python3 tools/oracle/replay_summary.py $O/$c.txt > $O/$c.summary.txt
  echo "$c: $(grep '^total' $O/$c.summary.txt)" >> $O/ALL.txt
done
