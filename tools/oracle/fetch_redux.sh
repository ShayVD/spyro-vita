#!/usr/bin/env bash
# Fetch the pinned PCSX-Redux build used as the behaviour oracle (porting-plan
# §7.3, step 13) and print the path of its executable. macOS arm64 build, run
# natively (decisions D22). PCSX-Redux is GPL: it runs out of process only and
# is never linked.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
ver=a5d621af
url=https://distrib.app/storage/assets/721/27f/0e3/b5dbde6a449844579a5b7f20c627f62a2ea1f7013de5db3ff47288b/PCSX-Redux-a5d621af-Arm.dmg
sha1=b442d0eb3bf049d471aea3d54680613e7ccc428a
dest="$root/.tools/pcsx-redux/$ver"
exe="$dest/PCSX-Redux.app/Contents/MacOS/PCSX-Redux"
if [ ! -x "$exe" ]; then
  [ "$(uname -s)-$(uname -m)" = Darwin-arm64 ] || { echo "fetch_redux: only the macOS arm64 build is pinned" >&2; exit 1; }
  mkdir -p "$dest"
  dmg="$dest/redux.dmg"
  [ -f "$dmg" ] || curl -fsSL --retry 3 -o "$dmg" "$url"
  got=$(shasum -a 1 "$dmg" | cut -d' ' -f1)
  [ "$got" = "$sha1" ] || { echo "fetch_redux: SHA-1 mismatch: $got" >&2; exit 1; }
  mnt=$(mktemp -d)
  hdiutil attach -nobrowse -readonly -mountpoint "$mnt" "$dmg" >/dev/null
  cp -R "$mnt/PCSX-Redux.app" "$dest/"
  hdiutil detach "$mnt" >/dev/null
  rm "$dmg"
fi
echo "$exe"
