#!/usr/bin/env bash
# Client for `spyro --step DIR` (src/platform/linux32/step.c): send one
# command and wait until the game stops again; prints the vblank it stopped at.
#   tools/step.sh DIR "up+cross 30"     (or "quit")
set -euo pipefail
dir=$1; shift
for _ in $(seq 600); do [ -f "$dir/ready" ] && break; sleep 0.1; done
[ -f "$dir/ready" ] || { echo "step: game not ready" >&2; exit 1; }
printf '%s\n' "$*" > "$dir/cmd.tmp" && mv "$dir/cmd.tmp" "$dir/cmd"
[ "$1" = quit ] && exit 0
for _ in $(seq 50); do [ -f "$dir/ready" ] || break; sleep 0.02; done
for _ in $(seq 3000); do [ -s "$dir/ready" ] && { cat "$dir/ready"; exit 0; }; sleep 0.1; done
echo "step: timed out" >&2; exit 1
