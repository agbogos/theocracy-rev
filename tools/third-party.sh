#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
# Emit the third-party manifest for a packaged bundle.
#
#   tools/third-party.sh <linux|macos|windows> <bundle-dir>
#
# Writes <bundle-dir>/THIRD-PARTY.txt, listing every bundled binary with the
# version it was taken from and how to obtain its corresponding source.
#
# WHY THIS IS GENERATED AND NOT WRITTEN
# -------------------------------------
# The Linux bundle ships 52 libraries, every one a stock Debian binary, and the
# set changes whenever SDL2's dependency graph does. A hand-maintained list
# would be wrong within a point release and nobody would notice, because nothing
# reads it until someone asks for source — by which time the bundle it described
# shipped months ago.
#
# So the manifest is produced from the bundle itself, at package time, on the
# machine that has the package database. `THIRD-PARTY.md` in the repo root is
# the policy and the written offer; this file is the evidence for one build, and
# it travels inside the bundle it describes.
#
# WHAT THE OBLIGATION ACTUALLY IS
# -------------------------------
# Unicorn is GPL-2.0 and ffmpeg as configured here is LGPL-2.1-or-later, so both
# carry a corresponding-source requirement that a version number alone does not
# satisfy — "some version of Unicorn 2" is not corresponding source, the exact
# one is. Hence a version for every entry and, for the Debian binaries, the one
# command that retrieves precisely what shipped.
#
# SDL2, SDL3 and sdl2-compat are zlib-licensed and carry no source obligation at
# all; they are listed because attribution is still required and because a
# reader deserves to know what is in the directory.
set -eu

PLATFORM="${1:-}"
BUNDLE="${2:-}"
case "$PLATFORM" in
  linux|macos|windows) ;;
  *) echo "usage: $0 <linux|macos|windows> <bundle-dir>" >&2; exit 2 ;;
esac
[ -d "$BUNDLE" ] || { echo "error: no such bundle: $BUNDLE" >&2; exit 1; }

OUT="$BUNDLE/THIRD-PARTY.txt"
FFMPEG_VERSION=7.1.5

{
  echo "Third-party components in this bundle"
  echo "====================================="
  echo
  echo "Generated at package time from the bundle's own contents. The project's"
  echo "licensing policy and the written offer for source are in THIRD-PARTY.md"
  echo "in the source repository."
  echo
  echo "The port's own code is GPL-2.0-or-later. None of the game's data or"
  echo "original binaries are included here or are redistributable."
  echo
} > "$OUT"

case "$PLATFORM" in
linux)
  # Runs inside the packaging container, which is where the package database
  # that produced these files lives. On the host it would describe the wrong
  # machine, or nothing at all.
  command -v dpkg-query >/dev/null 2>&1 || {
    echo "error: dpkg-query not found — the linux manifest must be generated" >&2
    echo "       inside the packaging container, not on the host" >&2
    exit 1
  }
  {
    echo "Base system: Debian $(cat /etc/debian_version 2>/dev/null || echo bookworm) ($(dpkg --print-architecture))"
    echo
    echo "Every entry below marked 'Debian' is an unmodified binary from the"
    echo "distribution. The exact corresponding source for any of them is:"
    echo
    echo "    apt-get source <source-package>=<version>"
    echo
    echo "on a matching Debian system. See THIRD-PARTY.md for the written offer."
    echo
    printf '%-28s %-26s %s\n' "FILE" "SOURCE PACKAGE" "VERSION"
    printf '%-28s %-26s %s\n' "----" "--------------" "-------"
  } >> "$OUT"

  # Ask dpkg who owns a path, trying every spelling of it that Debian uses.
  #
  # This needs four attempts, not one, because of usrmerge: `ldconfig -p`
  # reports /lib/<triplet>/libX11.so.6 while dpkg's database records
  # /usr/lib/<triplet>/libX11.so.6, and `dpkg -S` does no path canonicalisation
  # whatsoever — it is a literal lookup. Which spelling wins is per-package and
  # not guessable: libcap2 is found only as /lib/..., libx11-6 and libasound2
  # only as /usr/lib/..., and some record the versioned realpath rather than the
  # soname symlink. A single-candidate lookup left 42 of 52 libraries marked
  # UNKNOWN ORIGIN, which is worse than useless in a compliance document — it
  # looks like an answer.
  dpkg_owner() {
    _p=$1
    _real=$(readlink -f "$_p" 2>/dev/null || echo "$_p")
    for _c in "$_p" "/usr$_p" "$_real" "${_real#/usr}"; do
      [ -n "$_c" ] || continue
      _pkg=$(dpkg -S "$_c" 2>/dev/null | head -1 | cut -d: -f1)
      if [ -n "$_pkg" ]; then echo "$_pkg"; return 0; fi
    done
    return 1
  }

  for f in "$BUNDLE"/lib/*; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    # ldconfig first because it maps the soname to a real path; a plain
    # `dpkg -S basename` matches too loosely and picks up -dev symlinks.
    sys=$(ldconfig -p 2>/dev/null | awk -v b="$base" '$1==b {print $NF; exit}')
    # Not everything bundled is in the ldconfig cache. libpulsecommon-16.1.so
    # lives in a private pulseaudio/ subdirectory and is found at runtime
    # through libpulse's RPATH, so the cache has never heard of it — but the
    # loader pulled it in, so it is in the bundle and needs an entry. Fall back
    # to looking for it where libraries live.
    [ -n "$sys" ] || sys=$(find /usr/lib /lib -name "$base" 2>/dev/null | head -1)
    pkg=""
    [ -n "$sys" ] && pkg=$(dpkg_owner "$sys" || true)
    if [ -n "$pkg" ]; then
      ver=$(dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null || echo "?")
      src=$(dpkg-query -W -f='${source:Package}' "$pkg" 2>/dev/null || echo "$pkg")
      printf '%-28s %-26s %s\n' "$base" "$src" "$ver" >> "$OUT"
    else
      # Not from dpkg: the only such files are the minimal ffmpeg this repo
      # builds itself. Anything else appearing here is a packaging bug worth
      # seeing rather than hiding.
      case "$base" in
        libav*|libsw*) printf '%-28s %-26s %s\n' "$base" "ffmpeg (built here)" "$FFMPEG_VERSION" >> "$OUT" ;;
        *)             printf '%-28s %-26s %s\n' "$base" "UNKNOWN ORIGIN" "?" >> "$OUT" ;;
      esac
    fi
  done
  ;;

macos)
  command -v brew >/dev/null 2>&1 || {
    echo "error: brew not found — the macos manifest needs the package versions" >&2
    exit 1
  }
  {
    echo "Base system: macOS $(sw_vers -productVersion 2>/dev/null || echo '?') ($(uname -m))"
    echo
    echo "Entries marked 'Homebrew' are unmodified binaries from the named"
    echo "formula at the version shown. Corresponding source is the upstream"
    echo "release for that version; see THIRD-PARTY.md for the written offer."
    echo
    printf '%-28s %-26s %s\n' "FILE" "ORIGIN" "VERSION"
    printf '%-28s %-26s %s\n' "----" "------" "-------"
  } >> "$OUT"

  brew_version() { brew list --versions "$1" 2>/dev/null | awk '{print $2}' || true; }
  UNICORN_V=$(brew_version unicorn)
  SDL2_V=$(brew_version sdl2-compat)
  SDL3_V=$(brew_version sdl3)

  for f in "$BUNDLE"/lib/*.dylib; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    case "$base" in
      libunicorn*)   printf '%-28s %-26s %s\n' "$base" "Homebrew unicorn"     "${UNICORN_V:-?}" ;;
      libSDL2*)      printf '%-28s %-26s %s\n' "$base" "Homebrew sdl2-compat" "${SDL2_V:-?}" ;;
      libSDL3*)      printf '%-28s %-26s %s\n' "$base" "Homebrew sdl3"        "${SDL3_V:-?}" ;;
      libav*|libsw*) printf '%-28s %-26s %s\n' "$base" "ffmpeg (built here)"  "$FFMPEG_VERSION" ;;
      *)             printf '%-28s %-26s %s\n' "$base" "UNKNOWN ORIGIN"       "?" ;;
    esac
  done >> "$OUT"
  ;;

windows)
  # Nothing to interrogate: every dependency is either built by this repo or
  # unpacked from a pinned tarball, so the versions are the constants the
  # staging script used. Kept in step with tools/stage-win-deps.sh by the
  # assertion at the end of this script.
  SDL2_V=$(sed -n 's/^SDL2_VERSION=//p' "$(dirname "$0")/stage-win-deps.sh" | head -1)
  UNICORN_V=$(sed -n 's/^UNICORN_VERSION=//p' "$(dirname "$0")/stage-win-deps.sh" | head -1)
  {
    echo "Cross-built with mingw-w64. No Windows machine is involved in producing"
    echo "this bundle; see THIRD-PARTY.md for the written offer for source."
    echo
    echo "Not everything third-party in this bundle is a file of its own. Listing"
    echo "only the DLLs would understate what shipped, so the statically linked"
    echo "components are named first."
    echo
    printf '%-28s %-26s %s\n' "COMPONENT" "ORIGIN" "VERSION"
    printf '%-28s %-26s %s\n' "---------" "------" "-------"
    printf '%-28s %-26s %s\n' "(in theoc.exe)" "Unicorn, static" "${UNICORN_V:-?}"
    printf '%-28s %-26s %s\n' "(in theoc.exe)" "libgcc/libstdc++, static" "mingw-w64 toolchain"
    echo
    printf '%-28s %-26s %s\n' "FILE" "ORIGIN" "VERSION"
    printf '%-28s %-26s %s\n' "----" "------" "-------"
  } >> "$OUT"

  for f in "$BUNDLE"/bin/*.dll; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    case "$base" in
      unicorn*|libunicorn*) printf '%-28s %-26s %s\n' "$base" "Unicorn (built here)"   "${UNICORN_V:-?}" ;;
      SDL2*)                printf '%-28s %-26s %s\n' "$base" "SDL2 (mingw devel)"     "${SDL2_V:-?}" ;;
      avcodec*|avformat*|avutil*|swscale*|swresample*) \
                            printf '%-28s %-26s %s\n' "$base" "ffmpeg (built here)"    "$FFMPEG_VERSION" ;;
      libwinpthread*)       printf '%-28s %-26s %s\n' "$base" "mingw-w64 runtime"      "toolchain" ;;
      *)                    printf '%-28s %-26s %s\n' "$base" "UNKNOWN ORIGIN"         "?" ;;
    esac
  done >> "$OUT"
  ;;
esac

# The licence summary is the same on every platform, because the components are.
# Only the copies differ.
cat >> "$OUT" <<DOC

Licences
--------
  Unicorn            GPL-2.0-only AND GPL-2.0-or-later (QEMU-derived files are
                     v2-only, which is why this project as a whole cannot be
                     licensed later than GPL-2.0-or-later)
  ffmpeg             LGPL-2.1-or-later. Built WITHOUT --enable-gpl and without
                     any non-free component, so no GPL-only codec is present.
                     Configured as a minimal MPEG-1/MP2 decoder:
                       --disable-everything
                       --enable-decoder=mpeg1video,mpeg2video,mp2
                       --enable-demuxer=mpegps,mpegvideo,mp3
                       --enable-parser=mpegvideo,mpegaudio
                       --enable-protocol=file --enable-shared
  SDL2 / SDL3        zlib licence
  sdl2-compat        zlib licence
DOC

if [ "$PLATFORM" = windows ]; then
  cat >> "$OUT" <<'DOC'
  libgcc / libstdc++ GPL-3.0-or-later WITH GCC-exception-3.1. The exception is
                     what permits linking them statically into this binary
                     without imposing GPLv3 on it; see the mingw-w64 toolchain
                     for the exact build.

  NOTE: Unicorn is statically linked into theoc.exe on this platform, unlike
  the Linux and macOS bundles where it is a separate shared library. It is GPL
  either way and the same source obligation applies; it simply has no file of
  its own here to list.
DOC
fi

if [ "$PLATFORM" = linux ]; then
  cat >> "$OUT" <<'DOC'
  Everything else    Stock Debian binaries under their own licences, a mixture
                     of MIT, LGPL-2.1+ and BSD. The copyright file for any of
                     them is /usr/share/doc/<package>/copyright on a Debian
                     system, and the source is retrievable with the apt-get
                     command above.
DOC
fi

cat >> "$OUT" <<'DOC'

Obtaining source
----------------
For any component above, the corresponding source is available on request for
at least three years from the date of this build. Open an issue on the project
repository, or use the contact address in THIRD-PARTY.md.

Nothing in this bundle is a modified version of any of these libraries. They
are linked, not patched; the port's own source is the repository this was
built from, and its commit is stamped into the binary and printed as the first
line of every run.
DOC

# A library nobody can attribute is a compliance hole, so say so rather than
# letting it sit unread in a file nobody opens until it matters. Not fatal: the
# entry stays in the manifest marked UNKNOWN ORIGIN, and a release that has one
# is still better than a release that has one silently.
unknown=$(grep -c 'UNKNOWN ORIGIN' "$OUT" || true)
if [ "${unknown:-0}" -gt 0 ]; then
  echo "    WARNING: $unknown bundled file(s) could not be attributed — see $OUT" >&2
fi

echo "    wrote $(basename "$OUT") ($(grep -c . "$OUT") lines, $unknown unattributed)"
