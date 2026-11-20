# The PS4 toolchain in Docker, native arm64 (PS4 plan P2, D180;
# docs/ps4-build.md). The Mac's native set-up is tools/ps4/sdk.sh; both use
# the same OpenOrbis tarball and clang 18.
#   docker build --platform linux/arm64 -t spyro-ps4 -f tools/docker/ps4.Dockerfile tools/docker
#   tools/ps4/tc <command>   (runs <command> in the container, cwd = repo)
# - OpenOrbis PS4 Toolchain v0.5.4 (GPL-3.0; musl MIT, libc++ Apache-2.0
#   with LLVM exception), pinned by SHA-256: headers, libs, crt1.o, link.x.
# - Its tools are x86-64 in the release, so the image builds them for arm64
#   from pinned commits: create-fself v1.3 and create-gp4 v1.0 (Go,
#   GPL-3.0). LibOrbisPkg's PkgTool.Core 0.2.231 (LGPL-3.0, from the
#   tarball's framework-dependent .NET build) runs on Ubuntu's arm64 .NET 8
#   runtime (rolled forward from netcoreapp3.0).
# - clang-18 and lld-18 are Ubuntu 24.04's (OpenOrbis ships no compiler).
FROM --platform=linux/arm64 ubuntu:24.04
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      clang-18 lld-18 llvm-18 make python3 ca-certificates curl git file \
      golang-go dotnet-runtime-8.0 \
 && rm -rf /var/lib/apt/lists/*

ARG OO_VER=v0.5.4
ARG OO_SHA256=3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526
ENV OO_PS4_TOOLCHAIN=/opt/openorbis
RUN mkdir -p $OO_PS4_TOOLCHAIN \
 && curl -fsSL -o /tmp/oo.tar.gz https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/$OO_VER/toolchain-llvm-18.tar.gz \
 && echo "$OO_SHA256  /tmp/oo.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/oo.tar.gz -C $OO_PS4_TOOLCHAIN --strip-components=2 \
 && rm /tmp/oo.tar.gz

ARG FSELF_COMMIT=f2da6229684ce320f673c06fceac9c218f621788
ARG GP4_COMMIT=3f55f6af290ccc6b80b35632079ce9f98f0d63b9
ENV GOFLAGS=-trimpath GOTOOLCHAIN=local CGO_ENABLED=0 GOCACHE=/tmp/gocache GOPATH=/tmp/gopath
RUN mkdir -p $OO_PS4_TOOLCHAIN/bin/arm64 \
 && git clone -q https://github.com/OpenOrbis/create-fself /tmp/fself \
 && git -C /tmp/fself checkout -q $FSELF_COMMIT \
 && cd /tmp/fself/cmd/create-fself \
 && go build -modfile=go-linux.mod -o $OO_PS4_TOOLCHAIN/bin/arm64/create-fself \
 && git clone -q https://github.com/OpenOrbis/create-gp4 /tmp/gp4 \
 && git -C /tmp/gp4 checkout -q $GP4_COMMIT \
 && cd /tmp/gp4/cmd/create-gp4 \
 && go build -o $OO_PS4_TOOLCHAIN/bin/arm64/create-gp4 \
 && rm -rf /tmp/fself /tmp/gp4 /tmp/gocache /tmp/gopath
RUN printf '#!/bin/sh\nDOTNET_ROLL_FORWARD=Major DOTNET_CLI_TELEMETRY_OPTOUT=1 exec dotnet %s/bin/windows/PkgTool.Core.dll "$@"\n' \
      "$OO_PS4_TOOLCHAIN" > $OO_PS4_TOOLCHAIN/bin/arm64/PkgTool.Core \
 && chmod +x $OO_PS4_TOOLCHAIN/bin/arm64/PkgTool.Core

ENV PS4_CC=/usr/bin/clang-18 PS4_LD=/usr/bin/ld.lld-18 PS4_OOBIN=/opt/openorbis/bin/arm64
WORKDIR /work
