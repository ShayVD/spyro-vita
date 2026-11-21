# 32-bit Linux host debug build (docs/porting-plan.md §9 step 1, decisions D5, D14).
# Native arm64 image with an i686 cross compiler: python, cmake and gcc run
# natively and only the i386 game binary runs emulated (qemu-i386, through
# binfmt or explicitly). A linux/amd64 image segfaulted python3 under Docker
# Desktop's emulation, as in M0. The repo is mounted at /work.
#   docker build -t spyro-host -f tools/docker/host.Dockerfile tools/docker
#   tools/tc <command>     (runs <command> in the container, cwd = repo)
#   gdb: tools/tc qemu-i386 -g 1234 build/linux32/spyro ... & gdb-multiarch
# The linux64 build (PS4 plan P1, D174) adds clang (its __ptr32), the x86-64
# libc, libgcc, libstdc++ and binutils (the x86-64 cross gcc and g++) and runs
# under qemu-x86_64 (qemu-user's); cmake/linux64.toolchain.cmake.
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      gcc-i686-linux-gnu g++-i686-linux-gnu make cmake ninja-build \
      gdb-multiarch qemu-user patch python3 python3-yaml ca-certificates file \
      clang-19 gcc-x86-64-linux-gnu g++-x86-64-linux-gnu libc6-dev-amd64-cross \
 && rm -rf /var/lib/apt/lists/*
# The x86-64 cross libm.a is a linker script naming the native paths
# (/usr/lib/x86_64-linux-gnu/...); linux64 links statically through it.
RUN sed -i 's|/usr/lib/x86_64-linux-gnu/|/usr/x86_64-linux-gnu/lib/|g' /usr/x86_64-linux-gnu/lib/libm.a
ENV QEMU_LD_PREFIX=/usr/i686-linux-gnu
WORKDIR /work
