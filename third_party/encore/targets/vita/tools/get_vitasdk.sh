#!/usr/bin/env bash
# Fetch vitasdk into $VITASDK (default /usr/local/vitasdk) without docker or GitHub releases: the
# vitasdk/vitasdk image's toolchain and package layers (linux/amd64, channel 2026.08, pinned by
# digest), read from Docker Hub's Google mirror (mirror.gcr.io; Docker Hub itself rate-limits
# shared addresses) and unpacked.  Each layer's sha256 is checked.  On a Mac or a desktop, the
# usual vdpm install (https://vitasdk.org) is simpler. From kh1-decomp tools/vita/get_vitasdk.sh @ e1575e1a.
#   tools/vita/get_vitasdk.sh            # then: export VITASDK=/usr/local/vitasdk
set -euo pipefail

VITASDK="${VITASDK:-/usr/local/vitasdk}"
REG="${VITASDK_REGISTRY:-https://mirror.gcr.io}"
IMAGE="vitasdk/vitasdk"
# the toolchain (usr/local/vitasdk) and the packages (vita2d, the stubs, ...); the other layers
# are the image's Ubuntu
LAYERS=(
  64dba48febf5f5f94d7f88f389dee371c427bc7312b15dafceec03702246df26
  50e627109e7402c31a429100f054086cace0d7bae704dee7438e2d1e31cc15f5
)

if [[ -x "$VITASDK/bin/arm-vita-eabi-gcc" ]]; then
  echo "ok: vitasdk already in $VITASDK"; exit 0
fi
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$VITASDK"
for d in "${LAYERS[@]}"; do
  echo "fetching layer ${d:0:12}"
  curl -fsSL -o "$tmp/$d.tgz" "$REG/v2/$IMAGE/blobs/sha256:$d"
  if command -v sha256sum > /dev/null; then echo "$d  $tmp/$d.tgz" | sha256sum -c --quiet
  else echo "$d  $tmp/$d.tgz" | shasum -a 256 -c --quiet; fi
  # the layers hold usr/local/vitasdk/...: strip it to land in $VITASDK
  tar xzf "$tmp/$d.tgz" -C "$VITASDK" --strip-components=3 usr/local/vitasdk
  rm -f "$tmp/$d.tgz"
done
"$VITASDK/bin/arm-vita-eabi-gcc" --version | head -1
echo "ok: export VITASDK=$VITASDK"
