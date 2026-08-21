#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
# Build and package a relocatable macOS arm64 bundle of the port.
#
#   tools/package-macos.sh
#
# Output: dist/theoc-macos-arm64-<version>/ containing
#   theoc            the launcher (run this)
#   bin/theoc        the actual binary
#   lib/*.dylib      bundled dependencies
#   README.txt
#
# The bundle is always signed — ad-hoc by default, which is the minimum an
# arm64 Mac will load at all. Set THEOC_CODESIGN_IDENTITY to a Developer ID
# Application identity and it is signed for distribution instead, with the
# hardened runtime; notarisation is a separate step, done in CI on a zip of
# this directory (see .github/workflows/release.yml).
#
# NOT AN .app, AND THAT IS DELIBERATE
# -----------------------------------
# This ships a .tar.gz of a plain directory, the same shape as the Linux bundle,
# because the game needs a data tree sitting beside the launcher that the user
# supplies from their own CD. An .app that has to be told where its data lives
# is worse than a folder that obviously contains it. The cost is that the
# notarisation ticket cannot be stapled — `stapler` only writes tickets into
# app bundles, dmgs and pkgs — so Gatekeeper verifies online on first run. See
# the README's OFFLINE note.
#
# WHAT IS AND IS NOT BUNDLED
# --------------------------
# Everything outside /usr/lib and /System/Library, which on this platform is a
# far cleaner cut than the Linux bundle's hand-tuned denylist. macOS has no
# equivalent of the libX11/libGL hazard: the display server, audio and GPU are
# reached through system frameworks that live under /System/Library and are
# therefore never candidates. libc++ and libSystem are in /usr/lib and stay
# there, which is the same call the Linux bundle makes about glibc/libstdc++.
#
# THE ONE THING otool CANNOT TELL YOU
# -----------------------------------
# Homebrew's `sdl2` is an alias for `sdl2-compat`, which is a shim over SDL3 —
# and it does not *link* SDL3, it dlopens it, first candidate
# "@loader_path/libSDL3.dylib". So libSDL3 appears nowhere in the closure walk
# below and a bundle built purely from otool output launches, finds no SDL3, and
# dies at SDL_Init with "Failed loading SDL3 library". Same lesson the Linux
# bundle learned about libasound, in the opposite direction: the closure is
# necessary and not sufficient, so libSDL3 is added by name and then the load is
# actually exercised before the bundle is called good.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/port/build-macos-arm64"

[ "$(uname -s)" = Darwin ] || { echo "error: this packages a macOS bundle and needs a Mac" >&2; exit 1; }
# arm64 only, by decision. An Intel bundle would be a second native build on an
# Intel runner: the Homebrew dependencies are per-architecture, so there is no
# universal binary to be had without building the whole dependency set twice.
[ "$(uname -m)" = arm64 ] || {
  echo "error: arm64 only; this host is $(uname -m)" >&2
  exit 1
}

# Build identity, resolved here rather than in CMake so the bundle's directory
# name and the binary's banner can never disagree. Mirrors package-linux.sh
# exactly — including the 7-char clamp and the dirty flag, which must match
# port/CMakeLists.txt or two builds of one commit stamp different bytes.
VERSION=$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo unknown)
case "$VERSION" in
  *-dirty) echo ">>> WARNING: building $VERSION — uncommitted changes are in this bundle" >&2 ;;
esac
STAMP_DATE=$(git -C "$ROOT" log -1 --format=%cd --date=format:%y%m%d 2>/dev/null || echo "")
COMMIT=$(git -C "$ROOT" rev-parse --short=7 HEAD 2>/dev/null || echo "")
if [ -n "$COMMIT" ]; then
  COMMIT=$(printf %.7s "$COMMIT")
  if [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    COMMIT="$COMMIT+"
  else
    COMMIT="${COMMIT}0"
  fi
fi
OUT="$ROOT/dist/theoc-macos-arm64-$VERSION"

# A minimal ffmpeg, if one has been built, replaces Homebrew's. This is not a
# nicety here: Homebrew's ffmpeg drags x265, x264, svt-av1, libvpx, dav1d and
# openssl into the closure and the bundle goes from ~19 MB to 66 MB, none of it
# reachable by a decoder that only ever sees MPEG-1/MP2.
FFMPEG_MIN="${THEOC_FFMPEG_PREFIX:-$ROOT/port/deps-ffmpeg-macos-arm64}"
if [ -f "$FFMPEG_MIN/lib/libavcodec.dylib" ]; then
  echo ">>> using the minimal ffmpeg at $FFMPEG_MIN"
else
  echo ">>> no minimal ffmpeg — using Homebrew's (bundle will be ~66 MB)"
  echo "    build it with: tools/build-ffmpeg-min.sh macos"
  FFMPEG_MIN=""
fi

# Configure from scratch: find_library caches, so a tree left from a run with a
# different ffmpeg prefix keeps linking the old one and ships it silently.
rm -rf "$BUILD"
echo ">>> configuring"
cmake -S "$ROOT/port" -B "$BUILD" \
      -DTHEOC_FFMPEG_PREFIX="$FFMPEG_MIN" \
      -DTHEOC_VERSION="$VERSION" \
      -DTHEOC_STAMP_DATE="$STAMP_DATE" \
      -DTHEOC_COMMIT="$COMMIT" \
      -DCMAKE_BUILD_TYPE=Release >/dev/null

echo ">>> building"
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/dev/null
[ -f "$BUILD/theoc" ] || { echo "error: theoc was not produced" >&2; exit 1; }

echo ">>> resolving the dylib closure"
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/lib"
cp "$BUILD/theoc" "$OUT/bin/theoc"
chmod u+w "$OUT/bin/theoc"

# Anything here is part of macOS and is never bundled. /usr/lib covers libSystem
# and libc++; /System/Library covers every framework, which is how the display,
# audio and GPU are reached.
is_system() {
  case "$1" in
    /usr/lib/*|/System/*) return 0 ;;
    *) return 1 ;;
  esac
}

# Breadth-first over LC_LOAD_DYLIB, keyed on the *referenced* basename rather
# than the file it resolves to: that reference is the string dyld will look for,
# and Homebrew's opt/ symlinks mean the realpath often has a different name
# (libavcodec.61.dylib -> libavcodec.61.19.101.dylib). Copying with -L gives us
# the contents under the name that will actually be requested.
deps_of() {
  # Skips line 1, the file name otool echoes back. Everything already rewritten
  # to @rpath/@loader_path is skipped too, which is also how a library's own
  # LC_ID_DYLIB stays out: otool prints the id in this same list, so `take` sets
  # it to @rpath/<name> the moment a file is copied, before anything walks it.
  # Matching the id by basename instead does not work — SDL3 is installed as
  # libSDL3.0.dylib but must be bundled under the name libSDL2 dlopens,
  # libSDL3.dylib, so the two disagree and it collects itself.
  otool -L "$1" | tail -n +2 | sed -e 's/ (compatibility.*//' -e 's/^[[:space:]]*//' \
    | grep -v "^@"
}

# Copy one dylib in under the name dyld will ask for, and immediately claim it:
# writable, and identifying itself by @rpath.
take() {
  cp -L "$1" "$2"
  chmod u+w "$2"
  install_name_tool -id "@rpath/$(basename "$2")" "$2" 2>/dev/null
}

pending=$(deps_of "$OUT/bin/theoc")
seen=""
missing=""
while [ -n "$pending" ]; do
  next=""
  for dep in $pending; do
    base=$(basename "$dep")
    case " $seen " in *" $base "*) continue ;; esac
    seen="$seen $base"
    is_system "$dep" && continue
    if [ ! -f "$dep" ]; then
      missing="$missing $dep"
      continue
    fi
    take "$dep" "$OUT/lib/$base"
    echo "    bundled $base"
    next="$next $(deps_of "$OUT/lib/$base")"
  done
  pending=$next
done

# Never fail silently on a dependency we could not find: on the target this is a
# "dyld: Library not loaded" abort naming a path that does not exist there,
# which tells the user nothing they can act on.
if [ -n "$missing" ]; then
  echo "error: could not locate these non-system dylibs:$missing" >&2
  exit 1
fi

# SDL3, which the closure above cannot see — see the header comment. Resolved
# through the copied libSDL2's own directory first so a non-Homebrew SDL2 that
# was linked against a matching SDL3 still gets the right one.
if [ -f "$OUT/lib/libSDL2-2.0.0.dylib" ]; then
  SDL3=""
  for cand in \
      "$(dirname "$(readlink -f /opt/homebrew/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib 2>/dev/null || echo /nonexistent)")/libSDL3.dylib" \
      /opt/homebrew/opt/sdl3/lib/libSDL3.dylib \
      /usr/local/opt/sdl3/lib/libSDL3.dylib; do
    [ -f "$cand" ] && { SDL3="$cand"; break; }
  done
  [ -n "$SDL3" ] || {
    echo "error: libSDL2 is sdl2-compat and needs SDL3, which was not found" >&2
    echo "       brew install sdl3" >&2
    exit 1
  }
  take "$SDL3" "$OUT/lib/libSDL3.dylib"
  echo "    bundled libSDL3.dylib (dlopened by sdl2-compat, not in the closure)"
  # SDL3 has its own closure, and it is not empty on every install.
  for dep in $(deps_of "$OUT/lib/libSDL3.dylib"); do
    base=$(basename "$dep")
    is_system "$dep" && continue
    [ -f "$OUT/lib/$base" ] && continue
    [ -f "$dep" ] || { echo "error: SDL3 needs $dep, which is missing" >&2; exit 1; }
    take "$dep" "$OUT/lib/$base"
    echo "    bundled $base (via SDL3)"
  done
fi

echo ">>> making it relocatable"
# Every install name becomes @rpath/<basename>, and the two rpaths that resolve
# it are @executable_path/../lib on the binary and @loader_path on each dylib.
# Both, not either: dyld resolves @rpath using the run-path list of the main
# executable *and* of the library doing the loading, and libSDL3 arrives through
# a dlopen from libSDL2 rather than through the executable at all.
for f in "$OUT"/lib/*.dylib; do
  for dep in $(deps_of "$f"); do
    dbase=$(basename "$dep")
    [ -f "$OUT/lib/$dbase" ] || continue
    install_name_tool -change "$dep" "@rpath/$dbase" "$f" 2>/dev/null
  done
  install_name_tool -add_rpath "@loader_path" "$f" 2>/dev/null || true
done
for dep in $(deps_of "$OUT/bin/theoc"); do
  dbase=$(basename "$dep")
  [ -f "$OUT/lib/$dbase" ] || continue
  install_name_tool -change "$dep" "@rpath/$dbase" "$OUT/bin/theoc" 2>/dev/null
done
install_name_tool -add_rpath "@executable_path/../lib" "$OUT/bin/theoc" 2>/dev/null

# Sign. Always — this is not the optional part.
#
# install_name_tool rewrites load commands, and on Apple Silicon that
# invalidates the ad-hoc signature every dylib and every linker output already
# carries. An arm64 macOS refuses to map a dylib whose signature does not match
# its contents, and it does not refuse politely: the process is SIGKILLed with
# no diagnostic at all. So a bundle that skipped this step is not "unsigned",
# it is a bundle that cannot be loaded by anything, including the smoke test.
#
# With an identity: Developer ID, hardened runtime, secure timestamp — what
# notarisation requires. Without one: ad-hoc, which re-seals the files so they
# load locally and is all a development build needs. Either way, inside-out,
# because signing a binary seals the state of what it links.
if [ -n "${THEOC_CODESIGN_IDENTITY:-}" ]; then
  echo ">>> signing as $THEOC_CODESIGN_IDENTITY"
  for f in "$OUT"/lib/*.dylib; do
    codesign --force --timestamp --options runtime --sign "$THEOC_CODESIGN_IDENTITY" "$f"
  done
  # --entitlements only on the executable: entitlements are a property of the
  # process, and a dylib carrying them is at best ignored and at worst a
  # notarisation rejection.
  codesign --force --timestamp --options runtime \
           --entitlements "$ROOT/port/theoc.entitlements" \
           --sign "$THEOC_CODESIGN_IDENTITY" "$OUT/bin/theoc"
else
  echo ">>> ad-hoc signing (set THEOC_CODESIGN_IDENTITY for a distributable bundle)"
  for f in "$OUT"/lib/*.dylib; do codesign --force --sign - "$f"; done
  # --entitlements here too, though an ad-hoc bundle does not need them: it
  # makes codesign parse the file on every local build. It has to be codesign
  # that checks — `plutil -lint` calls port/theoc.entitlements valid when it
  # holds a double hyphen inside an XML comment, which is illegal XML, and
  # codesign rejects it with "AMFIUnserializeXML: syntax error". Discovered by
  # a CI run, because this was the one line of the signing path a local build
  # never exercised.
  #
  # No --options runtime, deliberately: the hardened runtime enforces library
  # validation, ad-hoc signatures carry no Team ID, and the result is a bundle
  # that cannot load its own dylibs. Distribution builds get it; this is a
  # development bundle and needs to run.
  codesign --force --entitlements "$ROOT/port/theoc.entitlements" --sign - "$OUT/bin/theoc"
fi

echo ">>> verifying the signatures"
for f in "$OUT"/lib/*.dylib; do codesign --verify --strict "$f"; done
codesign --verify --strict --verbose=2 "$OUT/bin/theoc" 2>&1 | sed 's/^/    /'

if [ -n "${THEOC_CODESIGN_IDENTITY:-}" ]; then
  # Asks the question Gatekeeper will ask, which --verify does not: an ad-hoc or
  # a Development identity seals correctly and is still refused here. Before the
  # notarytool step a rejection is expected — there is no ticket yet — so this
  # reports rather than fails, and the real gate is `spctl` on the downloaded
  # bundle after notarisation.
  echo ">>> what Gatekeeper makes of it (pre-notarisation)"
  spctl --assess --type exec --verbose=4 "$OUT/bin/theoc" 2>&1 | sed 's/^/    /' || true
fi

# Assert it, do not assume it. install_name_tool fails quietly in more ways than
# it fails loudly, and a single surviving absolute path is a bundle that works
# perfectly on the build machine and nowhere else — which is precisely the bug
# that cannot be caught by testing where it was built.
echo ">>> checking for absolute paths"
leaked=$(
  for f in "$OUT/bin/theoc" "$OUT"/lib/*.dylib; do
    otool -L "$f" | tail -n +2 | sed -e 's/ (compatibility.*//' -e 's/^[[:space:]]*//' \
      | grep -E '^(/opt/|/usr/local/|'"$(echo "$ROOT" | sed 's,/,\\/,g')"')' \
      | sed "s,^,    $(basename "$f"): ," || true
  done
)
if [ -n "$leaked" ]; then
  echo "error: build-machine paths survived in the bundle:" >&2
  echo "$leaked" >&2
  exit 1
fi

# Prove SDL2 can actually reach SDL3 from inside the bundle. SDL_Init(0)
# initialises no subsystem — no window, no audio, no display needed — but it is
# what makes sdl2-compat dlopen SDL3, so it fails loudly here if libSDL3 is
# missing or unloadable rather than silently on a player's machine. The smoke
# test cannot cover this: THEOC_FIX_SAVE returns from main before SDL is
# touched at all.
if [ -f "$OUT/lib/libSDL2-2.0.0.dylib" ]; then
  echo ">>> checking SDL2 -> SDL3"
  python3 - "$OUT/lib/libSDL2-2.0.0.dylib" <<'PY'
import ctypes, sys
lib = ctypes.CDLL(sys.argv[1])
lib.SDL_GetError.restype = ctypes.c_char_p
if lib.SDL_Init(0) != 0:
    sys.exit("    FAIL: SDL_Init: " + lib.SDL_GetError().decode())
print("    ok: sdl2-compat loaded SDL3 from the bundle")
PY
fi

cat >"$OUT/theoc" <<'LAUNCH'
#!/bin/sh
# Theocracy — guest-libmvos port. Run this, not bin/theoc.
here=$(cd "$(dirname "$0")" && pwd)
cd "$here"

# No LD_LIBRARY_PATH equivalent is set here on purpose. The bundled libraries
# are found through the LC_RPATH baked into the binary, which is the only
# mechanism that survives the hardened runtime — a signed binary has every
# DYLD_* variable stripped from its environment before it starts, so a launcher
# that relied on DYLD_LIBRARY_PATH would work unsigned and break once signed.

# Where the extracted game tree lives (default ./data/game).
THEOC_DATA="${THEOC_DATA:-$here/data/game}"
export THEOC_DATA

if [ ! -f "$here/data/cd/linux/theocracy.real" ]; then
  echo "theoc: missing $here/data/cd/linux/theocracy.real" >&2
  echo "       Put the original binaries in data/cd/linux/ and the extracted" >&2
  echo "       tree in data/game/ (or point THEOC_DATA elsewhere)." >&2
  exit 1
fi

# theoc.cfg sits beside this launcher, not beside the binary in bin/ — the
# binary looks next to itself and in the working directory, and neither is where
# a player will see the file. Point at it explicitly, without overriding a
# THEOC_CONFIG the user set themselves.
if [ -z "${THEOC_CONFIG:-}" ] && [ -f "$here/theoc.cfg" ]; then
  export THEOC_CONFIG="$here/theoc.cfg"
fi

exec "$here/bin/theoc" "$@"
LAUNCH
cp "$ROOT/port/theoc.cfg" "$OUT/theoc.cfg"
chmod +x "$OUT/theoc" "$OUT/bin/theoc"

cat >"$OUT/README.txt" <<TXT
Theocracy — guest-libmvos port (macOS, Apple Silicon)
=====================================================
Build: $VERSION
TXT
cat >>"$OUT/README.txt" <<'TXT'

Run:   ./theoc

WHAT THIS IS
    The original 1999 Linux i386 binaries — theocracy.real and libmvos.so —
    executed under Unicorn, with only the OS/library ABI emulated natively.
    It is not a reimplementation and not a Mac build of the game.

WHAT YOU MUST SUPPLY
    The game data is not included and is not redistributable. Put these
    beside this file:

      data/cd/linux/theocracy.real     the original game binary
      data/cd/linux/libmvos.so.0.9     the original engine
      data/cd/linux/server             (optional) the dedicated server
      data/game/                       the extracted CD data tree

    Override the data tree location with THEOC_DATA=/path/to/tree.

    For CD music, rip the audio tracks to a folder and point THEOC_CD_AUDIO
    at it. Tracks are found by the first number in the filename ("2 Audio
    Track.aiff" is disc track 2). Accepted: .aiff .aif .aifc .wav .flac
    .ogg .mp3 .m4a .ape .wv

REQUIREMENTS
    Apple Silicon (arm64) and macOS 14 or newer. There is no Intel build.

IF macOS REFUSES TO OPEN IT
    This bundle is signed and notarised, but the notarisation ticket is not
    stapled into it — Apple only staples tickets into .app bundles, .dmg and
    .pkg, and this is a plain folder. So the first launch asks Apple's servers
    whether the binary is notarised, and needs a working network connection to
    do it. Offline, or if the download picked up a quarantine flag that
    confuses things, clear it:

      xattr -dr com.apple.quarantine .

    run from inside this folder. That is the whole fix; there is no need to
    turn off Gatekeeper.

BUNDLED IN lib/
    Unicorn, SDL2 (with SDL3 beneath it) and a cut-down ffmpeg holding only the
    MPEG-1/MP2 decoder the cutscenes need. Everything else — Metal, CoreAudio,
    AppKit, libc++ — comes from macOS itself, so your GPU and audio stack are
    always the system's.

SETTINGS
    Edit theoc.cfg beside this file. Every line in it is commented out, so it
    changes nothing until you remove a #. Environment variables still win over
    it, so anything below keeps working as a one-off.

USEFUL KNOBS (full list in docs/porting/diagnostics.md)
      THEOC_FULLSCREEN=1      borderless fullscreen, 4:3 pillarboxed
      THEOC_SKIP_MOVIES=1     skip the intro cutscenes
      THEOC_FPS=1             per-second frame/throughput report on stderr
      THEOC_WATCHDOG=1        first thing to reach for if it ever freezes
      THEOC_VERBOSE=1         the full host boot log (quiet by default)
      THEOC_PROVINCE_MS=50    province at 20fps / 1.67x speed (see the docs:
                              this scales game speed with frame rate)
      THEOC_SERVER=1          run the dedicated server instead of the game
TXT

echo ">>> third-party manifest"
"$ROOT/tools/third-party.sh" macos "$OUT"

SIZE=$(du -sh "$OUT" | cut -f1)
echo ">>> done: $OUT ($SIZE, build $VERSION)"
echo "    Every run prints '$VERSION' in its first log line, so a tester's log"
echo "    identifies its own build."
