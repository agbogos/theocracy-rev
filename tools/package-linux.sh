#!/bin/sh
# Build and package a relocatable Linux bundle of the port.
#
#   tools/package-linux.sh [amd64|arm64]     (default: amd64)
#
# Output: dist/theoc-linux-<arch>/ containing
#   theoc            the launcher (run this)
#   bin/theoc        the actual binary
#   lib/*.so*        bundled dependencies
#   README.txt
#
# Runs entirely in a container, so `ldd` sees the *target* architecture rather
# than the build host's. On Apple Silicon, arm64 is native and amd64 goes
# through Rancher's qemu binfmt — correct either way, just slower.
#
# WHAT IS AND IS NOT BUNDLED, and why it matters
# ----------------------------------------------
# `ldd` reports ~160 libraries, because SDL2 pulls in the whole X11 / Wayland /
# PulseAudio / GL stack. Bundling those would be actively harmful: they are
# *system integration* libraries that must match the machine's display server,
# sound daemon and GPU drivers. A bundled libX11 or libGL talking to a different
# host stack is how you get a black screen or no audio on someone else's distro.
#
# So the denylist below keeps those on the host, and everything else — Unicorn,
# SDL2 itself, the ffmpeg family and their codec dependencies — is bundled,
# because those are the versions this binary was linked against and are the ones
# least likely to be installed.
#
# glibc and libstdc++ are deliberately NOT bundled. Mixing a bundled libstdc++
# with the host's libGL (which also uses it) is a classic way to break GPU
# drivers. The cost is a floor on the target: it needs a glibc and libstdc++ at
# least as new as Debian bookworm's (glibc 2.36, GCC 12). If the target is
# older, build the image FROM an older base rather than bundling around it.
set -eu

ARCH="${1:-amd64}"
case "$ARCH" in
  amd64|arm64) ;;
  *) echo "usage: $0 [amd64|arm64]" >&2; exit 2 ;;
esac

REPO="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="theoc-linux-$ARCH"
OUT="dist/theoc-linux-$ARCH"

: "${CONTAINER:=nerdctl}"   # Rancher Desktop on macOS; set CONTAINER=docker elsewhere

echo "==> building image $IMAGE (linux/$ARCH)"
"$CONTAINER" build --platform "linux/$ARCH" -t "$IMAGE" "$REPO"

echo "==> building and packaging in the container"
"$CONTAINER" run --rm --platform "linux/$ARCH" -v "$REPO:/src" "$IMAGE" sh -c '
set -eu
ARCH="'"$ARCH"'"
OUT="'"$OUT"'"
BUILD="port/build-linux-$ARCH"

cmake -S port -B "$BUILD" >/dev/null
cmake --build "$BUILD" -j"$(nproc)" >/dev/null
echo "    built $BUILD/theoc"

rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/lib"
cp "$BUILD/theoc" "$OUT/bin/theoc"

# Only the C/C++ runtimes stay on the host. Everything else is bundled.
#
# This denylist was arrived at empirically, after a hand-written one failed: it
# excluded libasound on the theory that SDL dlopens it, and the bundle would not
# start at all because Debian SDL2 has it as a hard DT_NEEDED. Guessing which
# dependencies are dlopen-ed does not work; the loader is the oracle.
#
# The libraries that would be genuinely dangerous to bundle are the GL/driver
# dispatch ones — and they are not in the graph, because SDL2 dlopens GL at
# runtime. So GPU acceleration comes from the host no matter what we ship. What
# is left (X11, xcb, wayland, alsa, pulse, dbus, va, drm) are all *client*
# libraries speaking stable protocols to host daemons, which is the same
# trade-off the Steam runtime makes.
#
# The residual risk is libdrm/libva against an unusual GPU stack; the launcher
# takes THEOC_SYSTEM_LIBS=1 to ignore the bundle if that ever bites.
DENY="^(ld-linux|libc\.so|libm\.so|libpthread|libdl\.so|librt\.so|libresolv|libnsl|libanl|libstdc\+\+|libgcc_s)"

n=0
ldd "$OUT/bin/theoc" | awk "/=> \//{print \$3}" | sort -u | while read -r so; do
  base=$(basename "$so")
  if echo "$base" | grep -Eq "$DENY"; then continue; fi
  cp -L "$so" "$OUT/lib/$base"
  echo "$base"
done > /tmp/bundled.txt
n=$(wc -l < /tmp/bundled.txt)
echo "    bundled $n libraries (host provides display/audio/GL/libc)"
sed "s/^/      /" /tmp/bundled.txt

cat > "$OUT/theoc" <<"LAUNCH"
#!/bin/sh
# Theocracy — guest-libmvos port. Run this, not bin/theoc.
here=$(cd "$(dirname "$0")" && pwd)
cd "$here"

# Prefer our bundled libs. OpenGL is deliberately absent from the bundle --
# SDL2 dlopens it -- so the host GPU stack is always used.
# THEOC_SYSTEM_LIBS=1 ignores the bundle entirely, for a machine whose own
# libraries work better (an unusual GPU/driver stack, say).
if [ -z "${THEOC_SYSTEM_LIBS:-}" ]; then
  LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export LD_LIBRARY_PATH
fi

# Where the extracted game tree lives (default ./data/game).
THEOC_DATA="${THEOC_DATA:-$here/data/game}"
export THEOC_DATA

if [ ! -f "$here/data/cd/linux/theocracy.real" ]; then
  echo "theoc: missing $here/data/cd/linux/theocracy.real" >&2
  echo "       Put the original binaries in data/cd/linux/ and the extracted" >&2
  echo "       tree in data/game/ (or point THEOC_DATA elsewhere)." >&2
  exit 1
fi

exec "$here/bin/theoc" "$@"
LAUNCH
chmod +x "$OUT/theoc" "$OUT/bin/theoc"

cat > "$OUT/README.txt" <<"DOC"
Theocracy — guest-libmvos port (Linux)
======================================

Run:   ./theoc

Layout expected next to this file:

  data/cd/linux/theocracy.real     the original game binary
  data/cd/linux/libmvos.so.0.9     the original engine
  data/cd/linux/server             (optional) the dedicated server
  data/game/                       the extracted CD data tree

None of that is shipped here — supply it from your own copy of the game.
Override the data tree location with THEOC_DATA=/path/to/tree.

Bundled in lib/: Unicorn, SDL2, ffmpeg, and the client libraries they need
(X11, Wayland, ALSA, PulseAudio, dbus, ...). OpenGL is NOT bundled -- SDL2
loads it at run time -- so your GPU drivers are always used.
Provided by your system: glibc and libstdc++. Needs glibc >= 2.36 and
libstdc++ from GCC 12 or newer.

If the bundled libraries misbehave on your machine, THEOC_SYSTEM_LIBS=1
ignores them and uses yours instead.

Useful knobs (full list in docs/porting/diagnostics.md):
  THEOC_FULLSCREEN=1      borderless fullscreen, 4:3 pillarboxed
  THEOC_SKIP_MOVIES=1     skip the intro cutscenes
  THEOC_FPS=1             per-second frame/throughput report on stderr
  THEOC_WATCHDOG=1        first thing to reach for if it ever freezes
  THEOC_PROVINCE_MS=50    province at 20fps / 1.67x speed (see the docs:
                          this scales game speed with frame rate)
  THEOC_SERVER=1          run the dedicated server instead of the game
DOC
echo "    wrote $OUT/theoc and $OUT/README.txt"
'

echo
echo "==> done: $REPO/$OUT"
du -sh "$REPO/$OUT" 2>/dev/null || true
