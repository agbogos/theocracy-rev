# Linux build + headless-test environment for the Theocracy port.
#
# This is the Linux-port workspace: it builds `theoc` and runs the *headless*
# verification (boot, link, present, pixel dumps) with no display, no VM and no
# SSH. A real Linux machine is only needed later, for interactive play and for
# the x86-64 differential-test oracle — see docs/porting/other-os-ports.md.
#
#   Build the image:   docker build -t theoc-linux .
#   Get a shell:       docker run --rm -it -v "$PWD:/src" theoc-linux
#   One-shot build:    docker run --rm -v "$PWD:/src" theoc-linux \
#                        cmake -S port -B port/build-linux && \
#                        cmake --build port/build-linux
#
# The repo is BIND-MOUNTED at /src rather than COPYied in, so edits on the host
# are picked up immediately and build output lands back in the tree. Use a build
# directory separate from the macOS one (`port/build-linux`), or the two will
# fight over CMakeCache.txt.
#
# On Apple Silicon this is a native linux/arm64 image — the host architecture is
# irrelevant to correctness, because Unicorn emulates the i386 guest either way.
# Nothing 32-bit is required to *build or run the port*; i386 multiarch only
# matters for the optional oracle, which is not this image's job.
FROM debian:bookworm-slim

# nasm is not for the port — it is for tools/build-ffmpeg-min.sh, which builds
# the bundle's minimal ffmpeg in this image. ffmpeg's configure *fails* on x86-64
# without an assembler ("nasm/yasm not found or too old"), so an amd64 bundle
# build stops here rather than in the port. It is absent on arm64 builds' critical
# path (no x86 asm to assemble) but costs nothing to have in both.
#
# libunicorn-dev in bookworm is Unicorn 2.x, which is what the host requires.
# Verified at image build time below rather than trusted — a silent fall back to
# 1.x would fail deep in machine.cpp with a confusing error.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        git \
        libunicorn-dev \
        libsdl2-dev \
        libavformat-dev \
        libavcodec-dev \
        libavutil-dev \
        libswscale-dev \
        libswresample-dev \
        nasm \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# Fail loudly at image-build time if Unicorn is not 2.x.
RUN set -eu; \
    v="$(grep -oP '(?<=#define UC_API_MAJOR )\d+' /usr/include/unicorn/unicorn.h || echo 0)"; \
    echo "unicorn UC_API_MAJOR=$v"; \
    [ "$v" -ge 2 ] || { echo "ERROR: Unicorn 2.x required, found major $v." >&2; \
                        echo "Build Unicorn from source and rebuild this image." >&2; exit 1; }

# Headless defaults. SDL's dummy video driver lets the port boot, present and
# dump frames (THEOC_SHOT_EVERY / THEOC_SHOT_DIR) with no X server at all — the
# same technique that verified sharp-bilinear on macOS without a display.
# Override SDL_VIDEODRIVER when running against a real X/Wayland display.
ENV SDL_VIDEODRIVER=dummy \
    SDL_AUDIODRIVER=dummy

WORKDIR /src
CMD ["/bin/bash"]
