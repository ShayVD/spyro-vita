# The game repo's host image (templates/game-repo/tools/docker/host.Dockerfile) with the 64-bit build added: clang for
# __ptr32, the x86-64 cross gcc, g++, libc and binutils, and qemu-x86_64 (qemu-user's) to run the result. A game copies
# the lines between the "lp64:" markers into its own tools/docker/host.Dockerfile; this file builds as it is, and the
# kit's lp64 tests run in it:
#   docker build -t lp64-host -f lp64/cmake/host-lp64.Dockerfile lp64/cmake
#   docker run --rm -v "$PWD":/work lp64-host lp64/test/run_tests.sh
# Native image (arm64 on an Apple-Silicon Mac), as the template's: the x86-64 tools are cross packages, which Debian
# has on arm64 but not on amd64 (there, the native gcc and libc6-dev are the x86-64 ones; install clang-19 and
# gcc-multilib and leave out the cross packages and the libm.a fix).
# From spyro-vita tools/docker/host.Dockerfile @ c25a3ea (MIT; its D174, where the owner approved these packages).
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      gcc-i686-linux-gnu g++-i686-linux-gnu make cmake ninja-build \
      gdb-multiarch qemu-user patch python3 python3-yaml ca-certificates file \
# lp64: begin (clang for __ptr32; the x86-64 libc, libgcc, libstdc++ and binutils with the cross gcc and g++)
      clang-19 gcc-x86-64-linux-gnu g++-x86-64-linux-gnu libc6-dev-amd64-cross \
# lp64: end
 && rm -rf /var/lib/apt/lists/*
# lp64: begin (the x86-64 cross libm.a is a linker script naming the native paths; a static link goes through it)
RUN sed -i 's|/usr/lib/x86_64-linux-gnu/|/usr/x86_64-linux-gnu/lib/|g' /usr/x86_64-linux-gnu/lib/libm.a
# lp64: end
ENV QEMU_LD_PREFIX=/usr/i686-linux-gnu
WORKDIR /work
