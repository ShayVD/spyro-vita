#!/bin/sh
# Differential sweeps of the hand-written pete functions against their mips2c
# units (decisions D33; src/game/oracle_replay.c SPYRO_REPLAY_SWEEP/POKE):
# the samples of a capture with Spyro's state, and the words their branches
# depend on, set to each value. Needs a build with the reference units:
#   tools/tc cmake -S . -B build/linux32-ref -G Ninja -DSPYRO_HOST=linux32 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/linux32.toolchain.cmake -DSPYRO_M2C_REF=ON
# Usage: [SETS="base fly disp"] tools/oracle/sweep_pete.sh CAPTURE OUTDIR [BUILDDIR]
# base: func_80043FE4's states; fly: its flying-ability phases; disp:
# func_80047B60. A run takes ~0.4 s under qemu: give fly and disp a small
# sample set (logs/run9-sweepset: 12 + 18 samples).
# g_Spyro 80078A58: +78 m_State, +7C m_walkingState, +8 z, +B0 m_slopeAngle,
# +11C/+120/+124 heading, pitch, roll, +164 m_health, +168 m_drowningOffset,
# +170 m_SurfaceProximityState, +1F4 m_ControlFlags, +224
# m_mobyInUseBySpyro, +24C m_flyingAbility, +254 unk_0x254 (manoeuvre half).
# g_LevelMobys 80075828, g_IsFlightLevel 80075690, g_LevelId 8007596C,
# D_80075700/D_800758A0 the flying ability's d-pad stick (D_80075700 also the
# turn-around's height), D_800758C0 the flight ceiling, g_Pad 80077378 (+8
# held, +1C no movement button). Whirlwind: D_80075668 bottom, D_80075724
# height and D_800757F0 turns of the spiral (the asm divides by D_80075724,
# so with it 0 the reference traps: CRASH-REF, not compared).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CAP=$1; O=$2; BUILD=${3:-build/linux32-ref}
cd "$ROOT"; mkdir -p "$O"
B=$BUILD/src/platform/linux32/spyro
MAP=$BUILD/oracle/fnmap.txt
if [ ! -f "$MAP" ] || [ "$B" -nt "$MAP" ]; then
  mkdir -p "$BUILD/oracle"
  tools/tc sh -c "i686-linux-gnu-nm $B > $BUILD/oracle/nm.txt"
  python3 tools/oracle/fnmap.py build/s1-matching/build/psx.map "$BUILD/oracle/nm.txt" > "$MAP"
fi
M=80078C7C=@80075828
FLY=80078CA4=1
Z=$(python3 -c "print(','.join(str(v) for v in range(-0x30000, 0x60000, 0x4000)))")
A=$(python3 -c "print(','.join(str(v) for v in range(-0x800, 0x801, 0x100)))")
SETS=${SETS:-base fly disp}
has() { case " $SETS " in *" $1 "*) return 0;; esac; return 1; }
run() { # NAME FUNCTION POKE SWEEP
  tools/tc env SPYRO_REPLAY_POKE="$3" SPYRO_REPLAY_SWEEP="$4" $B --replay "$CAP" --only $2 \
    --ignore tools/oracle/ignore_ranges.txt --fnmap "$MAP" > "$O/$1.txt" 2>&1 || true
  echo "$1: $(tail -1 "$O/$1.txt")"
}
P=func_80043FE4
if has base; then
run state $P "" 80078AD0=0-44
run state-26 $P "$M" 80078AD0=26
run ws-15 $P 80078AD0=15,80078CA4=0 80078AD4=0-12
run ws-3 $P 80078AD0=3 80078AD4=0-8
run ws-11 $P 80078AD0=11 80078AD4=0-3
run ws-20 $P 80078AD0=20 80078AD4=0,1,64,65
run ws-26 $P 80078AD0=26,$M 80078AD4=0-2
run ws-5 $P 80078AD0=5 80078AD4=0-1
run fly-6 $P 80078AD0=6 80078CA4=0-1
run wind-17 $P 80078AD0=17,$M 80075668=$Z
run wind-17-spiral $P 80078AD0=17,$M,80075724=0x2000,800757F0=0x1800 80075668=$Z
fi
# The flying ability's phases (states 15, 23, 32, 33, 34 with m_flyingAbility).
if has fly; then
for S in 15 23 32 33 34; do
  run fly-ws-$S $P 80078AD0=$S,$FLY 80078AD4=-1,0-12
  run fly-ws-$S-half2 $P 80078AD0=$S,$FLY,80078CAC=1 80078AD4=0-3,10
done
run fly-flightlevel $P 80078AD0=32,$FLY,80075690=1 80078AD4=0-3,10
# (Not g_LevelId 64 here: the replay maps the level's overlay function
# pointers by it, so g_SpawnParticle would point into the wrong overlay.)
run fly-groundlevel $P 80078AD0=33,$FLY,80075690=0 80078AD4=0-3,10
run fly-pitch-0 $P 80078AD0=32,$FLY,80078AD4=0 80078B78=$A
run fly-pitch-1 $P 80078AD0=32,$FLY,80078AD4=1 80078B78=$A
run fly-pitch-1-half2 $P 80078AD0=32,$FLY,80078AD4=1,80078CAC=1 80078B78=$A
run fly-pitch-2 $P 80078AD0=32,$FLY,80078AD4=2 80078B78=$A
run fly-pitch-2-half2 $P 80078AD0=32,$FLY,80078AD4=2,80078CAC=1 80078B78=$A
run fly-pitch-2-level $P 80078AD0=32,$FLY,80078AD4=2,80078CAC=1,80078B7C=0,80075700=@80078A60 80078B78=-256,-128,-64,0,64,127,128,256
run fly-pitch-3 $P 80078AD0=32,$FLY,80078AD4=3 80078B78=$A
run fly-pitch-10 $P 80078AD0=32,$FLY,80078AD4=10 80078B78=$A
run fly-pitch-10-half2 $P 80078AD0=32,$FLY,80078AD4=10,80078CAC=1 80078B78=$A
run fly-roll-0 $P 80078AD0=32,$FLY,80078AD4=0 80078B7C=$A
run fly-roll-2-half2 $P 80078AD0=32,$FLY,80078AD4=2,80078CAC=1 80078B7C=$A
run fly-roll-3 $P 80078AD0=32,$FLY,80078AD4=3,80078B78=1024 80078B7C=$A
run fly-dpad-y $P 80078AD0=32,$FLY,80078AD4=0 80075700=-127,-120,-64,-16,0,16,64,120,127
run fly-dpad-x $P 80078AD0=32,$FLY,80078AD4=0 800758A0=-127,-120,-64,-16,0,16,64,120,127
run fly-wake $P 80078AD0=32,$FLY,80078AD4=0 80078BC8=0,1
run fly-speed $P 80078AD0=32,$FLY,80078AD4=10 80078B70=0,0x700,0x1880,0x1900,0x1980,0x3000
# Free flight past the flight area's edge (x), and low with ground ahead
# (z under a high m_highestFlightPoint, +158; diving: m_Acceleration.z, +FC).
run fly-area $P 80078AD0=32,$FLY,80078AD4=0,80075690=1 80078A58=0,0x8000,0x10000,0x1F800,0x30000,0x40000,0x50000,-0x10000
run fly-low $P 80078AD0=32,$FLY,80078AD4=0,80078BB0=0x10000 80078A60=0x400,0x800,0x1000,0x2000,0x3000,0x4000,0x6000,0x8000
run fly-low-dive $P 80078AD0=32,$FLY,80078AD4=0,80078BB0=0x10000,80078B54=-0x4000 80078A60=0x400,0x800,0x1000,0x2000,0x3000,0x4000,0x6000,0x8000
fi
# func_80047B60, the per-frame collision dispatch.
Q=func_80047B60
if has disp; then
run disp-state $Q "" 80078AD0=0-44
run disp-fly $Q $FLY 80078AD0=0-44
run disp-fly-flightlevel $Q $FLY,80075690=1 80078AD0=0-44
run disp-fly-ws3 $Q $FLY,80075690=1,80078AD4=3 80078AD0=32-34
run disp-ceiling $Q $FLY,80078AD0=6,80075690=0 800758C0=0,1,0x1000,0x4000,0x8000,0x20000
run disp-ceiling-64 $Q $FLY,80078AD0=23,80075690=0,8007596C=64 800758C0=0,1,0x1000,0x4000,0x8000,0x20000
run disp-slope $Q 80078AD0=1 80078B08=0,1,5,10,15,22,30
run disp-drown $Q 80078AD0=29 80078BC0=0,1,8,0x90,0x96,0x238,0x240,0x300
run disp-drown-dead $Q 80078AD0=29,80078BBC=-1 80078BC0=0,1,8,0x90,0x96,0x238,0x240,0x300
run disp-portal $Q 80078AD0=15,80078C4C=0x4000 80078AD4=0-12
run disp-look $Q 80078AD0=0,80077394=1 80077380=0,0x10,0x1010
run disp-control $Q 80078AD0=0 80078C4C=0,1,0x4000
run disp-trail $Q 80078AD0=20 80078AD4=0,0x40,0x41
fi
