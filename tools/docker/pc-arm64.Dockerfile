# The PC build for Linux on ARM (docs/pc-build.md, D250 O5): the aarch64
# twin of pc.Dockerfile's Linux half, in the offset model (src/compat/p32.h),
# with SDL3 for the window, input and audio and OpenGL for the picture.
#
# An arm64 image: it runs natively on an Apple silicon Mac's Docker VM and
# on any aarch64 Linux. The same Debian 12 (glibc 2.36), clang 19 and SDL
# tag as pc.Dockerfile; no Windows half (llvm-mingw), which stays amd64's.
#   docker build --platform linux/arm64 -t spyro-pc-arm64 -f tools/docker/pc-arm64.Dockerfile tools/docker
#   PC_ARCH=arm64 tools/pc/tc <command>     (runs <command> in the container, cwd = repo)
FROM --platform=linux/arm64 debian:bookworm
ARG SDL_TAG=release-3.4.18
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      clang-19 lld-19 llvm-19 cmake ninja-build make patch git pkg-config \
      python3 python3-yaml ca-certificates curl xz-utils file \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
      libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
      libdecor-0-dev libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev \
      libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev \
      xvfb xauth x11-utils mesa-utils libgl1-mesa-dri libglx-mesa0 \
      imagemagick gdb \
 && rm -rf /var/lib/apt/lists/*
ENV CC=clang-19 CXX=clang++-19
# SDL3 for Linux: a static library, linked into the game (it loads X11,
# Wayland, GL, ALSA/PulseAudio/PipeWire itself at run time). Not PIC: the
# game is non-PIE (CMakeLists.txt).
RUN git clone --depth 1 -b ${SDL_TAG} https://github.com/libsdl-org/SDL.git /src/SDL \
 && cmake -S /src/SDL -B /src/SDL/build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
      -DCMAKE_POSITION_INDEPENDENT_CODE=OFF -DCMAKE_INSTALL_PREFIX=/opt/sdl3-linux \
 && cmake --build /src/SDL/build-linux && cmake --install /src/SDL/build-linux \
 && rm -rf /src/SDL/build-linux
WORKDIR /work
