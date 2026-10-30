#!/bin/sh
# Differential sweeps of func_8003EA68 / func_8003FE40 hand C against their
# mips2c units (SPYRO_M2C_REF build). Usage: tools/oracle/sweep_state.sh OUTDIR
# The captures it names are read from logs/caps/ (docs/translation-log.md);
# tools/oracle/sweep_totals.py OUTDIR sums the results.
cd "$(dirname "$0")/../.."
O=$1; mkdir -p "$O"
B=build/linux32-ref; BIN=$B/src/platform/linux32/spyro; chmod +x $BIN
mkdir -p $B/oracle
tools/tc sh -c "i686-linux-gnu-nm $BIN > $B/oracle/nm.txt"
python3 tools/oracle/fnmap.py build/s1-matching/build/psx.map $B/oracle/nm.txt > $B/oracle/fnmap.txt
ALL="cap3 cap5-fairy cap6-tour run1-handcov2 run3-handcov3 run4-handcov4 run13-oracle-routeA run14-oracle-routeB run14-oracle-tour run19-oracle-labels"
SOME="cap6-tour run1-handcov2"
HC="run1-handcov2 run3-handcov3 run4-handcov4 run13-oracle-routeA run14-oracle-routeB"
# run NAME FUNC CAPS POKE SWEEP SWEEP_REG
run() {
  for c in $3; do
    if tail -1 "$O/$1_$c.txt" 2>/dev/null | grep -q "^replay: [0-9]"; then echo "$1 $c: $(tail -1 "$O/$1_$c.txt")"; continue; fi
    e=""; [ -n "$4" ] && e="$e SPYRO_REPLAY_POKE=$4"; [ -n "$5" ] && e="$e SPYRO_REPLAY_SWEEP=$5"; [ -n "$6" ] && e="$e SPYRO_REPLAY_SWEEP_REG=$6"
    ( tools/tc env $e $BIN --replay logs/caps/$c --only $2 \
      --ignore tools/oracle/ignore_ranges.txt --fnmap $B/oracle/fnmap.txt --disc disc/files > "$O/$1_$c.txt" 2>&1 || true
    echo "$1 $c: $(tail -1 "$O/$1_$c.txt")" ) &
  done
  wait
}
S=80078A58 # g_Spyro
ST=80078AD0; WS=80078AD4; AIR=80078AF4; FIT=80078AF0; SLOPE=80078B08; INV=80078BB8
U194=80078BEC; CF=80078C4C; U208Z=80078C68; FALL=80078C70; MOBY=80078C7C; SUPER=80078CA8
M=$MOBY=@80075828
F=func_8003EA68
run ea-new $F "$ALL" "" "" a0=0-47
run ea-old $F "$HC" "" $ST=0-44 ""
for old in 0 6 15 23 29 32 33 34; do run ea-old$old $F "$SOME" $ST=$old "" a0=5,15,23,32-34; done
run ea-flight6 $F "$SOME" $ST=6,80075690=1 "" a0=15,23,32
for ws in 0 128; do for sc in 0 1; do run ea-super$sc-ws$ws $F "$SOME" $WS=$ws,$SUPER=$sc "" a0=11,20,44; done; done
for air in 0 1; do run ea-air$air $F "$SOME" $AIR=$air "" a0=7,14,22,28,30,31; done
for sl in 0 5 11 22 30; do run ea-slope$sl $F "$SOME" $SLOPE=$sl "" a0=1-3,21,26; done
run ea-u194 $F "$SOME" $U194=1 "" a0=1-3,21
run ea-moby17 $F "$SOME" $M "" a0=17
run ea-nomoby17 $F "$SOME" $MOBY=0 "" a0=17
for z in 0 5; do run ea-z$z $F "$SOME" $U208Z=$z "" a0=22,28; done
for i in 0 5; do run ea-inv$i $F "$SOME" $INV=$i "" a0=29; done
for f in 0 1; do run ea-fall$f $F "$SOME" $FIT=$f,$SLOPE=4 "" a0=6,16; done
for cam in 0 0x400 0x800 0xC00; do run ea-cam$cam $F "$SOME" 80076E20=$cam "" a0=11; done
G=func_8003FE40
run fe-state $G "$HC" "" $ST=0-44 ""
run fe-st20-ws $G "$SOME" $ST=20 $WS=0,64 ""
# m_ControlFlags with bit 31: each bit and some combinations (as signed decimals)
CFL=$(python3 -c "print(','.join(str(v-0x100000000) for v in [0x80000000|b for b in [0,1,2,4,7,0x41,0x42,0x44,0x47,8,0x10,0x20,0x38,0x100,0x200,0x400,0x1000,0x8000,0x10000,0x20000,0x4000,0x4007,0x2000,0x139,0x8047]]))")
run fe-cf $G "$SOME" "" $CF=0,1,0x4000,$CFL ""
run fe-cf-fall $G "$SOME" $FALL=5 $CF=$CFL ""
run fe-cf-moby $G "$SOME" $M $CF=$CFL ""
CAMS=$(python3 -c "print(','.join(str(v) for v in range(0, 0x1000, 0x100)))")
for t in 0 40; do for st in 1 26; do
  run fe-follow-t$t-st$st $G "$SOME" $M,80075948=$t,$ST=$st,$CF=-2147479552 80076E20=$CAMS ""
done; done
run fe-follow-lvl20 $G "$SOME" $M,80075948=40,8007596C=20,$CF=-2147479552 80076E20=$CAMS ""
