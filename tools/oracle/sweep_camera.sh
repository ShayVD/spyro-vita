#!/bin/sh
# Differential sweeps of the camera's hand C (func_80034CE8, func_80035FB4)
# against their mips2c units (decisions D33; src/game/oracle_replay.c
# SPYRO_REPLAY_SWEEP/POKE), for the paths the captures don't reach: every
# capture so far runs the passive camera (D_80075914 0x52), and none has
# state 6, 15, look mode or the scripted states. Needs a -DSPYRO_M2C_REF=ON
# build. Usage: tools/oracle/sweep_camera.sh CAPTURE OUTDIR [BUILDDIR]
# (SWEEP_JOBS, default 4: replays run side by side). Every line of OUTDIR's
# files must be PASS (CRASH-REF: the reference itself crashed, not compared).
#
# g_Camera 80076DD0: +58 m_State (80076E28), +C4 m_NudgeTicks (80076E94),
# +C8 m_SpyroOffCenterFrames (80076E98), +CC m_AtDestination (80076E9C),
# +DC/+E0 look yaw/pitch (80076EAC/B0), +F0 m_OnMovingPlatform (80076EC0).
# g_Spyro 80078A58: +78 m_State (80078AD0), +7C m_walkingState (80078AD4),
# +118 speed (80078B70), +11C heading (80078B74), +148 m_TurnMomentum
# (80078BA0), +1F4 m_ControlFlags (80078C4C), +224 m_mobyInUseBySpyro
# (80078C7C), +24C m_flyingAbility (80078CA4). D_80075914 camera mode (2
# active, 0x52 passive), D_8007592C, D_80075894 passive hold timer,
# D_800756B8 Spyro visible, D_80075924 L2/R2 speed. g_Pad 80077378 (+8
# m_Held 80077380, +18 m_NoButtonsDown 80077390, +44 m_BufferedInputs[0]
# 800773BC), g_ActivePad 800757E0, g_GameTick 8007572C, g_LevelMobys
# 80075828. SWEEP values are signed (strtol on i686): camera states
# 0x80000000.. are -2147483648..
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CAP=$1; O=$2; BD=${3:-build/linux32-ref}
cd "$ROOT"; mkdir -p "$O"
B=$BD/src/platform/linux32/spyro
MAP=$BD/oracle/fnmap.txt
if [ ! -f "$MAP" ] || [ "$B" -nt "$MAP" ]; then
  mkdir -p "$BD/oracle"
  tools/tc sh -c "i686-linux-gnu-nm $B > $BD/oracle/nm.txt"
  python3 tools/oracle/fnmap.py build/s1-matching/build/psx.map "$BD/oracle/nm.txt" > "$MAP"
fi
# Every camera state func_80035FB4 handles, and one it doesn't (0x80000012).
ST=0,1,2,3,4,5,6,7,15,-2147483648,-2147483641,-2147483639,-2147483638,-2147483636,-2147483632,-2147483631,-2147483630
M=80078C7C=@80075828
J=${SWEEP_JOBS:-4}; N=0
run() { # NAME FUNC POKES SWEEP
  (tools/tc env SPYRO_REPLAY_POKE="$3" SPYRO_REPLAY_SWEEP="$4" $B --replay "$CAP" --only "$2" \
    --ignore tools/oracle/ignore_ranges.txt --fnmap "$MAP" > "$O/$1.txt" 2>&1 || true
   echo "$1: $(tail -1 "$O/$1.txt")") &
  N=$((N + 1)); [ $((N % J)) -ne 0 ] || wait
}
for F in func_80035FB4 func_80034CE8; do
  f=${F#func_}
  for MODE in 2 0x52; do
    for NUDGE in 0 3; do
      run $f-st-m$MODE-n$NUDGE $F "80075914=$MODE,80076E94=$NUDGE,$M" 80076E28=$ST
    done
    run $f-st-m$MODE-auto $F "80075914=$MODE,8007592C=1,80078AD0=27,$M" 80076E28=$ST
    run $f-st-m$MODE-hold $F "80075914=$MODE,80075894=30,800756B8=1,80076EC0=0,80076E94=0,$M" 80076E28=$ST
    run $f-st-m$MODE-plat $F "80075914=$MODE,80076EC0=1,$M" 80076E28=$ST
    run $f-st-m$MODE-l2 $F "80075914=$MODE,80075924=1024,$M" 80076E28=$ST
    run $f-st-m$MODE-dest $F "80075914=$MODE,80076E9C=1,80076E94=0,$M" 80076E28=$ST
    run $f-st-m$MODE-hidden $F "80075914=$MODE,800756B8=0,$M" 80076E28=$ST
    # Spyro's state against the camera states that test it
    for CS in 0 3 6 -2147483639 -2147483631; do
      run $f-spyro-m$MODE-c$CS $F "80075914=$MODE,80076E28=$CS,80078B70=0x100,80076E94=0,80076EC0=0" 80078AD0=0-44
    done
    run $f-portal-m$MODE $F "80075914=$MODE,80078AD0=15,80076E28=0" 80078AD4=0-12
    run $f-speed-m$MODE $F "80075914=$MODE,80078AD0=5,80076E28=0,80076E94=0,80076EC0=0" 80078B70=0,0x3FF,0x400,0x1000
  done
done
# func_80035FB4's per-state inputs
F=func_80035FB4
H=$(python3 -c "print(','.join(str(v) for v in range(0, 4096, 256)))")
for WS in 0 1 2 3 11; do
  for FLY in 0 1; do
    run 35FB4-s5-ws$WS-f$FLY $F "80078AD4=$WS,80078CA4=$FLY,80076E28=5,80076E94=0" 80078B74=$H
  done
done
for CS in 1 2 4; do
  for MODE in 2 0x52; do
    run 35FB4-s$CS-m$MODE-head $F "80075914=$MODE,80076E28=$CS,80076E94=0" 80078B74=$H
  done
done
run 35FB4-s3-turn $F "80076E28=3,80076E94=0" 80078BA0=-6-6
run 35FB4-row $F "80076E28=0" 80076E98=0-12
run 35FB4-row-look $F "80076E28=-2147483639" 80076E98=0-12
run 35FB4-0A-ctrl $F "80076E28=-2147483638" 80078C4C=0,0x1000,0x200,0x1200
run 35FB4-follow-tick $F "80076E28=0,80076E94=3" 8007572C=0,119,120,5000
run 35FB4-follow-pad $F "80076E28=0,80075914=0x52,800756B8=1,80076EC0=0" 80077390=0,1
run 35FB4-follow-timer $F "80076E28=0,80075914=0x52,800756B8=1,80076EC0=0,80077390=1,80076E94=0" 80075894=0,1,2,30,90
run 35FB4-follow-timer-head $F "80076E28=0,80075914=0x52,800756B8=1,80076EC0=0,80077390=1,80076E94=0,80075894=1" 80078B74=$H
for M2 in 2 0x52; do
  run 35FB4-moby-m$M2 $F "80075914=$M2,$M,80076E94=0" 80076E28=-2147483641,-2147483636
done
# Look mode: d-pad (pad type 2) and stick (type 3) through m_BufferedInputs[0]
L="80076E28=0x80000009,80076E94=0,800757E0=0x800773BC"
run 35FB4-look-dpad $F "$L,800773BC=2" 80077380=0,0x1000,0x2000,0x4000,0x8000,0x3000,0xC000,0x9000,0x6000,0xF000
run 35FB4-look-stick $F "$L,800773BC=3" 800773D0=0x7F7F0000,0,-65536,0x7F000000,-8454144,0x00FF0000,0x7F7F7F7F,0x40C00000
run 35FB4-look-yaw $F "$L,800773BC=2,80077380=0" 80076EAC=-0x500,-0x400,-0x3FF,0,0x3FF,0x400,0x500
run 35FB4-look-pitch $F "$L,800773BC=2,80077380=0" 80076EB0=-0x500,-0x3D0,-0x200,-0x141,-0x140,0,0x200,0x500,0x600
wait
