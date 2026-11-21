#!/usr/bin/env bash
# The PS4 toolchain, natively on the Mac (PS4 plan P2, D180; docs/ps4-build.md).
# Downloads and checks the pinned pieces into $SPYRO_PS4_SDK
# (default ~/.local/share/spyro-ps4), outside the repo:
#   - OpenOrbis PS4 Toolchain v0.5.4 (GPL-3.0; musl MIT, libc++ Apache-2.0
#     with LLVM exception): headers, libc, library stubs, crt1.o, link.x,
#     sce_module/ PRXs, and its macOS tools (create-fself, create-gp4: Go,
#     GPL-3.0; LibOrbisPkg's PkgTool.Core: .NET, LGPL-3.0), x86-64 binaries
#     that run under Rosetta 2;
#   - LLVM 18.1.8's official arm64 macOS release (clang, ld.lld; Apache-2.0
#     with LLVM exception). OpenOrbis ships no compiler and Apple's clang
#     can't target it; Homebrew's llvm@18 has no bottle for macOS 14.
# The Docker set-up (tools/docker/ps4.Dockerfile) pins the same OpenOrbis
# tarball. Usage:
#   tools/ps4/sdk.sh            (idempotent; prints the environment to use)
#   eval "$(tools/ps4/sdk.sh --env)"
set -euo pipefail

SDK=${SPYRO_PS4_SDK:-$HOME/.local/share/spyro-ps4}
OO_VER=v0.5.4
OO_URL=https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/$OO_VER/toolchain-llvm-18.tar.gz
OO_SHA256=3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526
LLVM_VER=18.1.8
LLVM_TAR=clang+llvm-$LLVM_VER-arm64-apple-macos11.tar.xz
LLVM_URL=https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VER/$LLVM_TAR
LLVM_SHA256=4573b7f25f46d2a9c8882993f091c52f416c83271db6f5b213c93f0bd0346a10

OO=$SDK/openorbis-$OO_VER
LLVM=$SDK/llvm-$LLVM_VER

env_lines() {
  echo "export OO_PS4_TOOLCHAIN='$OO'"
  echo "export PS4_CC='$LLVM/bin/clang'"
  echo "export PS4_LD='$LLVM/bin/ld.lld'"
  echo "export PS4_OOBIN='$OO/bin/macos'"
}
if [ "${1:-}" = "--env" ]; then
  env_lines
  exit 0
fi

[ "$(uname -s)" = Darwin ] || { echo "sdk.sh is for the Mac; on Linux use tools/ps4/tc (Docker)" >&2; exit 1; }
mkdir -p "$SDK/dl"

fetch() { # url file sha256
  local f="$SDK/dl/$2"
  if [ ! -f "$f" ] || ! echo "$3  $f" | shasum -a 256 -c --status; then
    echo "downloading $1"
    curl -fSL -o "$f.part" "$1"
    mv "$f.part" "$f"
  fi
  echo "$3  $f" | shasum -a 256 -c --status || { echo "SHA-256 mismatch: $f" >&2; exit 1; }
}

fetch "$OO_URL" "openorbis-$OO_VER.tar.gz" "$OO_SHA256"
fetch "$LLVM_URL" "$LLVM_TAR" "$LLVM_SHA256"

if [ ! -f "$OO/link.x" ]; then
  rm -rf "$OO.part" && mkdir -p "$OO.part"
  tar -xzf "$SDK/dl/openorbis-$OO_VER.tar.gz" -C "$OO.part" --strip-components=2
  # the tools' names as the samples use them (the release names create-fself
  # create-fself-macos on the Mac)
  ln -sf create-fself-macos "$OO.part/bin/macos/create-fself"
  mv "$OO.part" "$OO"
fi
if [ ! -x "$LLVM/bin/clang" ]; then
  rm -rf "$LLVM.part" && mkdir -p "$LLVM.part"
  tar -xJf "$SDK/dl/$LLVM_TAR" -C "$LLVM.part" --strip-components=1
  mv "$LLVM.part" "$LLVM"
fi
# downloaded binaries carry the quarantine flag; the tools are run, not opened
xattr -dr com.apple.quarantine "$OO/bin/macos" "$LLVM/bin" 2>/dev/null || true

if ! /usr/bin/pgrep -q oahd; then
  echo "note: OpenOrbis's macOS tools are x86-64 and need Rosetta 2 (softwareupdate --install-rosetta)" >&2
fi
"$LLVM/bin/clang" --version | head -1 >&2
env_lines
