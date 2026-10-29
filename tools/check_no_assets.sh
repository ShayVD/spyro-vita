#!/usr/bin/env bash
# Refuse to commit game data or BIOS. Fails if any tracked or staged file is a
# PS-X EXE, an ELF, a raw CD image (sector sync pattern), an ISO, game data by
# name, or larger than 512 KiB. Run in CI and as a pre-commit hook:
#   ln -s ../../tools/check_no_assets.sh "$(git rev-parse --git-common-dir)/hooks/pre-commit"
# Same rules as kh-bbs-vita's and kh1-decomp's scripts, with PS1 formats.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

filesize() { stat -c %s "$1" 2>/dev/null || stat -f %z "$1"; }

fail=0
while IFS= read -r -d '' f; do
  [[ -f "$f" ]] || continue
  size=$(filesize "$f")
  magic=$(head -c 12 "$f" | od -An -tx1 | tr -d ' \n')
  case "$magic" in
    50532d582045584520*) echo "refused: $f is a PS-X EXE"; fail=1; continue ;;
    7f454c46*) echo "refused: $f is an ELF binary"; fail=1; continue ;;
    00ffffffffffffffffffff00) echo "refused: $f is a raw CD image"; fail=1; continue ;;
  esac
  if head -c 40000 "$f" | tail -c +32770 | head -c 5 | grep -q "CD001"; then
    echo "refused: $f looks like an ISO image"; fail=1
  elif [[ "$f" == disc/* || "$f" == *.STR || "$f" == *.ovl || "$f" == SCPH* || "$f" == *WAD.WAD ]]; then
    echo "refused: $f is game data or BIOS"; fail=1
  elif (( size > 524288 )); then
    echo "refused: $f is $size bytes (game data?)"; fail=1
  fi
done < <(git ls-files -z --cached)
exit $fail
