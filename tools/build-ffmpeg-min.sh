#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
# Build a minimal ffmpeg for the packaged bundles.
#
#   tools/build-ffmpeg-min.sh windows
#   tools/build-ffmpeg-min.sh linux [amd64|arm64]
#   tools/build-ffmpeg-min.sh macos            (native, arm64 only)
#
# Output: port/deps-ffmpeg-<target>/{include,lib,bin}, which the package scripts
# hand to cmake as -DTHEOC_FFMPEG_PREFIX=. Nothing else in the tree changes:
# development builds keep using the system ffmpeg, because THEOC_FFMPEG_PREFIX
# unset means "search as before".
#
# WHY
# ---
# A stock ffmpeg is ~90% of both bundles — 89.6 MB of avcodec on Windows, and on
# Linux the same codecs as separate DT_NEEDED .so files (x264, x265, vpx, theora,
# srt, zmq). None of it is reachable. This port decodes exactly one thing, and
# that is measured rather than assumed: all 27 files under data/cd/movie/ probe
# identically as
#
#     format mpeg (MPEG-PS) / mpeg1video / mp2 44100 Hz stereo
#
# so the configure line below is that list plus what port/src/mpeg.cpp calls
# directly — swscale for the YUV->RGB565 conversion and swresample for the audio
# format conversion. The engine's own FLC video never touches ffmpeg; it is guest
# code decoding into the LFB.
#
# --disable-everything switches off every codec/demuxer/parser/protocol and the
# enables put back what is used.
#
# THE SECOND HALF OF THE LIST IS THE CD AUDIO PATH
# The enable list was derived from `data/cd/movie/`, but the port has a second,
# unrelated libav consumer: `port/src/cdaudio.cpp` hands ripped CD audio to
# `avformat_open_input`, and it accepts ten container extensions
#
#     .aiff .aif .aifc .flac .wav .ogg .mp3 .m4a .ape .wv
#
# of which the minimal build could decode exactly none. `.mp3` looked supported
# and was not: the `mp3` *demuxer* was enabled for MPEG-PS probing, while the
# only audio *decoder* was `mp2`, which does not decode mp3.
#
# The raw `mpegvideo` and `mp3` demuxers are enabled on purpose: libavformat
# identifies an untyped MPEG-PS elementary stream by asking them to recognise
# their own bitstream, and without them the codec id stays NONE and no decoder
# is looked up. The probe reports MPEG-2 for MPEG-1 video, so `mpeg2video` is
# enabled too — it handles both and shares code with mpeg1video, so it is free.
#
# VERSION
# -------
# Pinned to 7.1.x, matching the sonames the staged Windows dependencies already
# use (avcodec-61, avutil-59, swresample-5, swscale-8) and Debian bookworm's API
# era. mpeg.cpp needs >= 5.1 regardless: it uses AVChannelLayout and
# swr_alloc_set_opts2, which are the 5.1 channel-layout API.
set -eu

TARGET="${1:-}"
ARCH="${2:-amd64}"
case "$TARGET" in
  windows) ;;
  # Native, so no arch argument: the runner and the bundle are both arm64. If an
  # Intel bundle is ever wanted it is a second native build on an Intel runner,
  # not a cross — ffmpeg's arm64 build uses the host assembler either way.
  macos) [ "$(uname -s)" = Darwin ] || { echo "error: macos target needs a Mac" >&2; exit 1 ; } ;;
  linux) case "$ARCH" in amd64|arm64) ;; *) echo "usage: $0 linux [amd64|arm64]" >&2; exit 2 ;; esac ;;
  *) echo "usage: $0 windows|linux|macos [amd64|arm64]" >&2; exit 2 ;;
esac

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=7.1.5
SRCDIR="$ROOT/port/ffmpeg-src"
TARBALL="$SRCDIR/ffmpeg-$VERSION.tar.xz"
SRC="$SRCDIR/ffmpeg-$VERSION"

case "$TARGET" in
  windows) PREFIX="$ROOT/port/deps-ffmpeg-win" ;;
  macos)   PREFIX="$ROOT/port/deps-ffmpeg-macos-arm64" ;;
  *)       PREFIX="$ROOT/port/deps-ffmpeg-linux-$ARCH" ;;
esac

# The whole point of the exercise: exactly what the port reaches for, nothing
# else. Keep this list and the docs/porting/other-os-ports.md table in step.
CONFIG_MIN="
  --disable-everything
  --enable-decoder=mpeg1video,mpeg2video,mp2
  --enable-demuxer=mpegps,mpegvideo,mp3
  --enable-parser=mpegvideo,mpegaudio
  --enable-decoder=pcm_s16be,pcm_s16le,pcm_s24be,pcm_s24le,pcm_u8,pcm_f32le
  --enable-decoder=flac,vorbis,opus,mp3,aac,alac,ape,wavpack
  --enable-demuxer=aiff,wav,flac,ogg,mov,ape,wv
  --enable-parser=flac,vorbis,opus,aac
  --enable-protocol=file
  --enable-shared
  --disable-static
  --disable-programs
  --disable-doc
  --disable-avdevice
  --disable-avfilter
  --disable-postproc
  --disable-network
  --disable-autodetect
  --disable-debug
"
# Collapse to one line. The list above is written multi-line to stay readable,
# but the Linux branch interpolates it into a `sh -c` string inside the
# container, where an embedded newline ends the command instead of separating
# an argument: configure then runs with NO flags — building the full-fat ffmpeg
# and succeeding — and the next line fails as `--disable-everything: not found`.
# A silent full build is exactly the failure this whole script exists to avoid.
CONFIG_MIN=$(echo $CONFIG_MIN)

# ---- source ------------------------------------------------------------------
# Kept inside the repo (CLAUDE.md: write only in here), gitignored the same way
# port/deps-win/ is — third-party source, fetched out of band, never committed.
mkdir -p "$SRCDIR"
if [ ! -f "$TARBALL" ]; then
  echo "==> fetching ffmpeg $VERSION"
  curl -fL --retry 3 -o "$TARBALL.part" "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
  mv "$TARBALL.part" "$TARBALL"
fi
if [ ! -d "$SRC" ]; then
  echo "==> extracting"
  tar -xJf "$TARBALL" -C "$SRCDIR"
fi

# ---- build -------------------------------------------------------------------
if [ "$TARGET" = windows ]; then
  command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || {
    echo "error: x86_64-w64-mingw32-gcc not found. brew install mingw-w64" >&2
    exit 1
  }
  BUILD="$SRCDIR/build-win"
  rm -rf "$BUILD" "$PREFIX"
  mkdir -p "$BUILD"
  echo "==> configuring (mingw-w64 cross)"
  ( cd "$BUILD" && "$SRC/configure" \
      --prefix="$PREFIX" \
      --enable-cross-compile \
      --cross-prefix=x86_64-w64-mingw32- \
      --arch=x86_64 \
      --target-os=mingw32 \
      $CONFIG_MIN >/dev/null )
  echo "==> building"
  make -C "$BUILD" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null
  make -C "$BUILD" install >/dev/null
  # Cross-built DLLs land in $PREFIX/bin; strip them, as the bundle does its own.
  x86_64-w64-mingw32-strip "$PREFIX"/bin/*.dll
elif [ "$TARGET" = macos ]; then
  # Native: no container, no cross-prefix. The only macOS-specific flag is the
  # install-name handling, and there is none — configure bakes an absolute path
  # into each dylib's LC_ID_DYLIB and tools/package-macos.sh rewrites every one
  # of them to @rpath anyway, exactly as it does for the Homebrew dylibs. Giving
  # ffmpeg different treatment here would mean two rewrite paths to get right
  # instead of one.
  BUILD="$SRCDIR/build-macos-arm64"
  rm -rf "$BUILD" "$PREFIX"
  mkdir -p "$BUILD"
  echo "==> configuring (native arm64)"
  ( cd "$BUILD" && "$SRC/configure" --prefix="$PREFIX" $CONFIG_MIN >/dev/null )
  echo "==> building"
  make -C "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/dev/null
  make -C "$BUILD" install >/dev/null
  # No strip. Apple's strip on a dylib without -x removes symbols codesign needs
  # to hash consistently, and the saving is ~100 KB across the whole family;
  # package-macos.sh signs these, so leave them exactly as linked.
else
  : "${CONTAINER:=nerdctl}"   # Rancher Desktop on macOS; CONTAINER=docker elsewhere
  IMAGE="theoc-linux-$ARCH"
  echo "==> building image $IMAGE (linux/$ARCH)"
  "$CONTAINER" build --platform "linux/$ARCH" -t "$IMAGE" "$ROOT" >/dev/null
  echo "==> configuring and building in the container"
  "$CONTAINER" run --rm --platform "linux/$ARCH" -v "$ROOT:/src" "$IMAGE" sh -c "
set -eu
BUILD=/src/port/ffmpeg-src/build-linux-$ARCH
PREFIX=/src/port/deps-ffmpeg-linux-$ARCH
rm -rf \"\$BUILD\" \"\$PREFIX\"
mkdir -p \"\$BUILD\"
cd \"\$BUILD\"
/src/port/ffmpeg-src/ffmpeg-$VERSION/configure --prefix=\"\$PREFIX\" $CONFIG_MIN >/dev/null
make -j\"\$(nproc)\" >/dev/null
make install >/dev/null
strip \"\$PREFIX\"/lib/*.so.* 2>/dev/null || true
"
fi

echo
echo "==> done: $PREFIX"
du -sh "$PREFIX" 2>/dev/null || true
ls -la "$PREFIX"/lib/*.dll.a 2>/dev/null | awk '{print "    " $5, $9}' || true
