#!/bin/sh
# The player's own LiveArea art (decisions D59): scales and crops two images
# into disc/livearea/ at the sizes the Vita wants, as 8-bit RGB PNGs. The
# Vita build converts them to indexed PNGs (make_livearea.py --from) and packs
# them instead of the original art. disc/ is gitignored: never commit this.
#
#   tools/vita/livearea_local.sh LOGO TITLE [GATE]
#     LOGO   a square logo image: the bubble icon (128x128)
#     TITLE  a picture for the LiveArea background (840x500)
#     GATE   a picture for the start gate (280x158); LOGO if left out
#   Each is cropped to its target's aspect around the middle, then scaled,
#   so crop first (sips -c H W --cropOffset Y X) to frame something else.
#
# macOS only (sips). Re-run cmake afterwards so the build sees the files.
set -eu
[ $# -eq 2 ] || [ $# -eq 3 ] || { sed -n '2,15p' "$0"; exit 2; }
logo=$1 title=$2 gate=${3:-$1}
out="$(cd "$(dirname "$0")/../.." && pwd)/disc/livearea"
mkdir -p "$out"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

size() { sips -g "pixel$2" "$1" | awk '/pixel/ {print $2}'; }

# crop SRC to the aspect AW:AH around its middle, then scale to AWxAH
fit() {
  src=$1 aw=$2 ah=$3 dst=$4
  w=$(size "$src" Width) h=$(size "$src" Height)
  if [ $((w * ah)) -gt $((h * aw)) ]; then
    cw=$((h * aw / ah)) ch=$h
  else
    cw=$w ch=$((w * ah / aw))
  fi
  sips -s format png "$src" --out "$tmp/src.png" >/dev/null
  sips -c "$ch" "$cw" "$tmp/src.png" --out "$tmp/crop.png" >/dev/null
  sips -s format png -z "$ah" "$aw" "$tmp/crop.png" --out "$dst" >/dev/null
}

fit "$logo" 128 128 "$out/icon0.png"
fit "$gate" 280 158 "$out/startup.png"
fit "$title" 840 500 "$out/bg.png"
echo "wrote $out/icon0.png, startup.png, bg.png"
