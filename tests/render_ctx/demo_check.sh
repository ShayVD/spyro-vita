#!/bin/sh
# Usage: demo_check.sh SPYRO DISC OUTDIR (stage 7d, D123)
# The attract demo's first 4,000 vblanks with --render-ctx-check: at least
# eight renderers checked, each with 0 differences and 0 writes, none trapped.
bin=$1; disc=$2; out=$3
mkdir -p "$out"
"$bin" --disc "$disc" --frames 4000 --render-ctx-check --log "$out/spyro.log" > "$out/stdout.txt" 2>&1 || {
  echo "render_ctx_demo: the run failed"; tail -5 "$out/stdout.txt"; exit 1; }
lines=$(grep -c "render ctx: func_[0-9A-F]*: [0-9]* calls" "$out/spyro.log")
good=$(grep -c "render ctx: func_[0-9A-F]*: [0-9]* calls, 0 differences, 0 writes to the game's memory (0 trapped)" "$out/spyro.log")
grep "render ctx: func_" "$out/spyro.log" | sed 's/^.*render ctx: /  /'
[ "$lines" -ge 8 ] && [ "$good" = "$lines" ] || { echo "render_ctx_demo: $good of $lines renderers clean"; exit 1; }
echo "render_ctx_demo: $lines renderers, all clean"
