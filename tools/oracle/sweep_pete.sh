#!/bin/sh
# Differential sweeps of func_80043FE4's hand C against its mips2c unit
# (decisions D33; src/game/oracle_replay.c SPYRO_REPLAY_SWEEP/POKE): the
# samples of a capture with Spyro's state, and the words its branches depend
# on, set to each value. Usage: tools/oracle/sweep_pete.sh CAPTURE OUTDIR
# g_Spyro 80078A58: +78 m_State, +7C m_walkingState, +224 m_mobyInUseBySpyro,
# +24C m_flyingAbility. g_LevelMobys 80075828. Whirlwind: D_80075668 bottom,
# D_80075724 height and D_800757F0 turns of the spiral (the asm divides by
# D_80075724, so with it 0 the reference traps: CRASH-REF, not compared).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CAP=$1; O=$2
cd "$ROOT"; mkdir -p "$O"
B=build/linux32/src/platform/linux32/spyro
M=80078C7C=@80075828
Z=$(python3 -c "print(','.join(str(v) for v in range(-0x30000, 0x60000, 0x4000)))")
run() {
  tools/tc env SPYRO_REPLAY_POKE="$2" SPYRO_REPLAY_SWEEP="$3" $B --replay "$CAP" --only func_80043FE4 \
    --ignore tools/oracle/ignore_ranges.txt --fnmap build/linux32/oracle/fnmap.txt > "$O/$1.txt" 2>&1 || true
  echo "$1: $(tail -1 "$O/$1.txt")"
}
run state "" 80078AD0=0-44
run state-26 "$M" 80078AD0=26
run ws-15 80078AD0=15,80078CA4=0 80078AD4=0-12
run ws-3 80078AD0=3 80078AD4=0-8
run ws-11 80078AD0=11 80078AD4=0-3
run ws-20 80078AD0=20 80078AD4=0,1,64,65
run ws-26 80078AD0=26,$M 80078AD4=0-2
run ws-5 80078AD0=5 80078AD4=0-1
run fly-6 80078AD0=6 80078CA4=0-1
run wind-17 80078AD0=17,$M 80075668=$Z
run wind-17-spiral 80078AD0=17,$M,80075724=0x2000,800757F0=0x1800 80075668=$Z
