#!/bin/sh
# Usage: demo_check.sh SPYRO DISC OUTDIR (stage 7e, D125)
# The attract demo to vblank 5,000 with --interp --pres-check (its play
# starts near vblank 3,900): at least 400 in-between frames in play drawn by
# the presenter and by D30's pass, every one identical record for record.
bin=$1; disc=$2; out=$3
mkdir -p "$out"
"$bin" --disc "$disc" --frames 5000 --interp --pres-check --log "$out/spyro.log" > "$out/stdout.txt" 2>&1 || {
  echo "pres_demo: the run failed"; tail -5 "$out/stdout.txt"; exit 1; }
line=$(grep "pres check: [0-9]* frames compared" "$out/spyro.log" | sed 's/^.*pres check: //')
echo "pres_demo: $line"
n=$(echo "$line" | sed -n 's/^\([0-9]*\) frames compared, \([0-9]*\) identical, \([0-9]*\) differ.*/\1 \2 \3/p')
set -- $n
[ "${1:-0}" -ge 400 ] && [ "$1" = "$2" ] && [ "$3" = 0 ] || { echo "pres_demo: not every frame identical"; exit 1; }
