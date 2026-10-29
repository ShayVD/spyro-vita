# 32-bit Linux host debug build (docs/porting-plan.md §9 step 1, decisions D5, D14).
# Native arm64 image with an i686 cross compiler: python, cmake and gcc run
# natively and only the i386 game binary runs emulated (qemu-i386, through
# binfmt or explicitly). A linux/amd64 image segfaulted python3 under Docker
# Desktop's emulation, as in M0. The repo is mounted at /work.
#   docker build -t spyro-host -f tools/docker/host.Dockerfile tools/docker
#   tools/tc <command>     (runs <command> in the container, cwd = repo)
#   gdb: tools/tc qemu-i386 -g 1234 build/linux32/spyro ... & gdb-multiarch
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      gcc-i686-linux-gnu g++-i686-linux-gnu make cmake ninja-build \
      gdb-multiarch qemu-user patch python3 python3-yaml ca-certificates file \
 && rm -rf /var/lib/apt/lists/*
ENV QEMU_LD_PREFIX=/usr/i686-linux-gnu
WORKDIR /work
