#!/bin/sh
# Every sweep suite (decisions D33, D37) on the REF build (-DSPYRO_M2C_REF=ON,
# build/linux32-ref). Usage: tools/oracle/sweep_all.sh [OUTDIR], default logs/run8-sweeps.
# Inputs (gitignored, docs/host-build.md): logs/caps/ captures, and the small
# sample sets the round-2 branches picked (logs/hc2-*: 11 camera samples,
# 12 flight/ground samples, 3 transition samples).
cd "$(dirname "$0")/../.."
O=${1:-logs/run8-sweeps}; mkdir -p $O
tools/tc cmake --build build/linux32-ref > $O/build.log 2>&1 || exit 1
B=build/linux32-ref; BIN=$B/src/platform/linux32/spyro; chmod +x $BIN
mkdir -p $B/oracle; tools/tc sh -c "i686-linux-gnu-nm $BIN > $B/oracle/nm.txt"
python3 tools/oracle/fnmap.py build/s1-matching/build/psx.map $B/oracle/nm.txt > $B/oracle/fnmap.txt
tools/oracle/sweep_camera.sh logs/hc2-camera/run3-sweep/cap $O/camera > $O/camera.log 2>&1
SETS=base tools/oracle/sweep_pete.sh logs/caps/run10-handcov $O/pete-base > $O/pete-base.log 2>&1
SETS="fly disp" tools/oracle/sweep_pete.sh logs/hc2-dispatch-47b60/run9-sweepset $O/pete-flydisp > $O/pete-flydisp.log 2>&1
for c in run1-handcov2 cap6-tour; do
  tools/tc env SPYRO_REPLAY_SWEEP=80078AD0=0-44 $BIN --replay logs/caps/$c --only func_80041670 \
    --ignore tools/oracle/ignore_ranges.txt --fnmap $B/oracle/fnmap.txt > $O/transition-state-$c.txt 2>&1
done
python3 tools/oracle/sweep_transition.py logs/hc2-dispatch-41670/run2-sweep/mini3 $O/transition --rounds 2 --seed 7 > $O/transition.log 2>&1
tools/oracle/sweep_state.sh $O/state > $O/state.log 2>&1
python3 tools/oracle/sweep_totals.py $O/state > $O/state-totals.txt 2>&1
echo done > $O/DONE
