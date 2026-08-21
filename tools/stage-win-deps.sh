#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
#
# Stage the Windows cross-build dependencies into port/deps-win/.
#
#   tools/stage-win-deps.sh              # stage into port/deps-win
#   THEOC_WIN_DEPS=/tmp/x tools/stage-win-deps.sh    # ...or somewhere else
#
# WHY THIS EXISTS
# ---------------
# port/deps-win/ was staged by hand on 2026-08-03 and is excluded by
# .gitignore, so `tools/package-windows.sh` worked on exactly one machine and
# nowhere else. That is the whole of what blocked Windows from CI. This script
# is the missing half: everything package-windows.sh assumes is already there.
#
# Idempotent — each component is skipped if it is already staged. Delete
# port/deps-win/ to force a re-stage.
#
# WHAT IS STAGED, AND WHAT DELIBERATELY IS NOT
# --------------------------------------------
# SDL2 and Unicorn go into port/deps-win/. **ffmpeg does not** — it is built
# separately into port/deps-ffmpeg-win/ by tools/build-ffmpeg-min.sh, which
# package-windows.sh searches first.
#
# tools/build-ffmpeg-min.sh builds LGPL-2.1+ with neither --enable-gpl
# nor --enable-nonfree — it is what takes the bundle from 119 MB to 7.3 MB.
#
# So this script does not download that zip, and nothing should put it back.
set -eu

SDL2_VERSION=2.32.10
UNICORN_VERSION=2.1.3

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="${THEOC_WIN_DEPS:-$ROOT/port/deps-win}"
SRC="$DEPS/src"
TRIPLE=x86_64-w64-mingw32

# Preflight. pkg-config is listed here for a specific reason: Unicorn's
# qemu/configure runs under CMake's execute_process with no error checking, so
# a missing pkg-config does not fail — it surfaces ~200 source files later as
# "config-target.h: No such file or directory", which reads as a Unicorn bug.
# Fail here instead, where the message is true.
for t in "$TRIPLE-gcc" "$TRIPLE-g++" cmake pkg-config curl tar; do
  command -v "$t" >/dev/null 2>&1 || {
    echo "error: $t not found." >&2
    case "$t" in
      $TRIPLE-*) echo "       brew install mingw-w64   (or: apt install mingw-w64)" >&2 ;;
      pkg-config) echo "       brew install pkg-config  — Unicorn's configure needs it," >&2
                  echo "       and fails ~200 files later without it, blaming something else." >&2 ;;
      *) echo "       install $t and try again." >&2 ;;
    esac
    exit 1
  }
done

mkdir -p "$DEPS" "$SRC"

# ---------------------------------------------------------------- SDL2 --------
# Download-and-untar: the mingw development tarball ships prebuilt import
# libraries for both architectures; take the 64-bit half.
if [ -d "$DEPS/include/SDL2" ]; then
  echo "==> SDL2 already staged"
else
  echo "==> SDL2 $SDL2_VERSION"
  TAR="$SRC/SDL2-devel-$SDL2_VERSION-mingw.tar.gz"
  URL="https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-devel-$SDL2_VERSION-mingw.tar.gz"
  [ -f "$TAR" ] || { curl -fL -sS --retry 3 -o "$TAR.part" "$URL" && mv -f "$TAR.part" "$TAR"; }
  rm -rf "$SRC/SDL2-$SDL2_VERSION"
  tar -xzf "$TAR" -C "$SRC"
  FROM="$SRC/SDL2-$SDL2_VERSION/$TRIPLE"
  [ -d "$FROM/include/SDL2" ] || { echo "error: unexpected SDL2 tarball layout at $FROM" >&2; exit 1; }
  for d in bin include lib share; do
    [ -d "$FROM/$d" ] || continue
    mkdir -p "$DEPS/$d"
    (cd "$FROM/$d" && tar -cf - .) | (cd "$DEPS/$d" && tar -xf -)
  done
  echo "    staged SDL2 -> $DEPS"
fi

# ------------------------------------------------------------- Unicorn --------
# Cross-built from source; there is no prebuilt mingw Unicorn to download.
#
#   -DUNICORN_ARCH=x86        the guest is i386 and nothing else. Cuts the build
#                             from every architecture QEMU supports to one.
#   -DBUILD_SHARED_LIBS=OFF   static, so the bundle carries one less DLL.
if [ -f "$DEPS/lib/libunicorn.a" ] && [ -d "$DEPS/include/unicorn" ]; then
  echo "==> Unicorn already staged"
else
  echo "==> Unicorn $UNICORN_VERSION (cross-build; this is the slow one)"
  TAR="$SRC/unicorn-$UNICORN_VERSION.tar.gz"
  URL="https://github.com/unicorn-engine/unicorn/archive/refs/tags/$UNICORN_VERSION.tar.gz"
  [ -f "$TAR" ] || { curl -fL -sS --retry 3 -o "$TAR.part" "$URL" && mv -f "$TAR.part" "$TAR"; }
  rm -rf "$SRC/unicorn-src"
  mkdir -p "$SRC/unicorn-src"
  tar -xzf "$TAR" -C "$SRC/unicorn-src" --strip-components=1

  BUILD="$SRC/unicorn-src/build-win"
  rm -rf "$BUILD"
  cmake -S "$SRC/unicorn-src" -B "$BUILD" \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER="$(command -v "$TRIPLE-gcc")" \
        -DCMAKE_CXX_COMPILER="$(command -v "$TRIPLE-g++")" \
        -DCMAKE_BUILD_TYPE=Release \
        -DUNICORN_ARCH=x86 \
        -DBUILD_SHARED_LIBS=OFF >/dev/null
  cmake --build "$BUILD" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null

  mkdir -p "$DEPS/lib" "$DEPS/include"
  for a in libunicorn.a libunicorn-common.a; do
    [ -f "$BUILD/$a" ] || { echo "error: Unicorn did not produce $a" >&2; exit 1; }
    cp "$BUILD/$a" "$DEPS/lib/$a"
  done
  rm -rf "$DEPS/include/unicorn"
  cp -R "$SRC/unicorn-src/include/unicorn" "$DEPS/include/unicorn"
  echo "    staged Unicorn -> $DEPS"
fi

# -------------------------------------------------------------- ffmpeg --------
# Not staged into deps-win; see the header. Built into port/deps-ffmpeg-win,
# which package-windows.sh searches first.
if [ -f "$ROOT/port/deps-ffmpeg-win/lib/libavcodec.dll.a" ]; then
  echo "==> minimal ffmpeg already built"
else
  echo "==> minimal ffmpeg (LGPL, tools/build-ffmpeg-min.sh)"
  "$ROOT/tools/build-ffmpeg-min.sh" windows
fi

# --------------------------------------------------------------- check --------
# Assert on the tree, not on the exit codes above: this is what
# package-windows.sh will actually look for.
fail=0
for f in "$DEPS/include/SDL2/SDL.h" \
         "$DEPS/lib/libSDL2.dll.a" \
         "$DEPS/include/unicorn/unicorn.h" \
         "$DEPS/lib/libunicorn.a" \
         "$ROOT/port/deps-ffmpeg-win/lib/libavcodec.dll.a"; do
  if [ -e "$f" ]; then echo "  ok      $f"
  else echo "  MISSING $f" >&2; fail=1; fi
done
[ "$fail" -eq 0 ] || { echo "error: staging incomplete" >&2; exit 1; }

echo "==> done: $DEPS"
echo "    now: tools/package-windows.sh"
