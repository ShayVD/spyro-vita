# The PC build (docs/pc-build.md): x86-64 Linux and Windows desktop builds
# of the game in the low-image model (D173, D174), with SDL3 for the window,
# input and audio and OpenGL for the picture.
#
# An amd64 image: the game is x86-64 only (clang's __ptr32), and its Linux
# build runs here natively, under Rosetta on an Apple Silicon Mac (a colima
# VM with --vz-rosetta; qemu works too, slowly). The Windows build is
# cross-compiled with llvm-mingw; GNU binutils for PE do the relocatable
# links and objcopy steps llvm's COFF tools don't (cmake/pc-windows.toolchain.cmake).
# Debian 12's glibc (2.36) is what the Linux binary needs at least.
#   docker build --platform linux/amd64 -t spyro-pc -f tools/docker/pc.Dockerfile tools/docker
#   tools/pc/tc <command>     (runs <command> in the container, cwd = repo)
FROM --platform=linux/amd64 debian:bookworm
ARG SDL_TAG=release-3.4.18
ARG LLVM_MINGW=20261104
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      clang-19 lld-19 llvm-19 cmake ninja-build make patch git pkg-config \
      python3 python3-yaml ca-certificates curl xz-utils file \
      binutils-mingw-w64-x86-64 \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
      libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
      libdecor-0-dev libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev \
      libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev \
      xvfb xauth x11-utils mesa-utils libgl1-mesa-dri libglx-mesa0 \
      imagemagick gdb \
 && rm -rf /var/lib/apt/lists/*
ENV CC=clang-19 CXX=clang++-19
# SDL3 for Linux: a static library, linked into the game (it loads X11,
# Wayland, GL, ALSA/PulseAudio/PipeWire itself at run time).
RUN git clone --depth 1 -b ${SDL_TAG} https://github.com/libsdl-org/SDL.git /src/SDL \
 && cmake -S /src/SDL -B /src/SDL/build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
      -DCMAKE_POSITION_INDEPENDENT_CODE=OFF -DCMAKE_INSTALL_PREFIX=/opt/sdl3-linux \
 && cmake --build /src/SDL/build-linux && cmake --install /src/SDL/build-linux
# llvm-mingw (clang for x86_64-w64-mingw32, UCRT) and SDL3 for Windows,
# static too.
RUN curl -fsSL https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW}/llvm-mingw-${LLVM_MINGW}-ucrt-ubuntu-22.04-x86_64.tar.xz \
      | tar -xJ -C /opt \
 && mv /opt/llvm-mingw-${LLVM_MINGW}-ucrt-ubuntu-22.04-x86_64 /opt/llvm-mingw
ENV PATH=/opt/llvm-mingw/bin:$PATH
RUN cmake -S /src/SDL -B /src/SDL/build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang \
      -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++ -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
      -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
      -DCMAKE_INSTALL_PREFIX=/opt/sdl3-win \
 && cmake --build /src/SDL/build-win && cmake --install /src/SDL/build-win \
 && rm -rf /src/SDL/build-linux /src/SDL/build-win
WORKDIR /work
