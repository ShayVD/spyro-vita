#!/bin/sh
# Usage: demo_check.sh SPYRO DISC OUTDIR (stage 7e, D125; since D129)
# The attract demo to vblank 5,000 with --interp --pres-check (the title,
# then its play from near vblank 3,900): at least 2,000 ticks whose frame
# the presenter drew from the snapshot unblended and the logic draw drew,
# every one identical record for record. (Until D129: the in-between
# frames in play, against D30's pass.)
bin=$1; disc=$2; out=$3
mkdir -p "$out"
"$bin" --disc "$disc" --frames 5000 --interp --pres-check --log "$out/spyro.log" > "$out/stdout.txt" 2>&1 || {
  echo "pres_demo: the run failed"; tail -5 "$out/stdout.txt"; exit 1; }
line=$(grep "pres check: [0-9]* frames compared" "$out/spyro.log" | sed 's/^.*pres check: //')
echo "pres_demo: $line"
n=$(echo "$line" | sed -n 's/^\([0-9]*\) frames compared, \([0-9]*\) identical, \([0-9]*\) differ.*/\1 \2 \3/p')
set -- $n
[ "${1:-0}" -ge 2000 ] && [ "$1" = "$2" ] && [ "$3" = 0 ] || { echo "pres_demo: not every frame identical"; exit 1; }
