#!/bin/sh
# Build and package a relocatable Windows x86-64 bundle of the port.
#
#   tools/package-windows.sh
#
# Output: dist/theoc-windows-x64/ containing
#   theoc.bat        the launcher (run this)
#   bin/theoc.exe    the actual binary
#   bin/*.dll        bundled dependencies
#   README.txt
#
# Cross-compiles from macOS or Linux with mingw-w64 — no Windows machine is
# involved in producing this. See docs/porting/other-os-ports.md.
#
# WHAT IS AND IS NOT BUNDLED
# --------------------------
# The Linux bundle learned this the hard way: guessing which dependencies are
# dlopen-ed does not work, the loader is the oracle. So the DLL list here is not
# hand-written — it is the transitive import closure of theoc.exe, computed with
# objdump and filtered against a denylist of things that ship *with Windows*.
#
# Two differences from the Linux bundle are worth knowing:
#
#   * The closure is SEVEN DLLs and stops, against the Linux bundle's sprawling
#     ~160-library graph. That is not because it carries less: ffmpeg's codec
#     dependencies (x264, x265, vpx, theora...) are *statically linked into*
#     avcodec-61.dll here rather than sitting beside it as separate .so files.
#     Measured, because the first version of this comment guessed and was wrong:
#     131 MB total, of which avcodec-61.dll alone is 89.6 MB and avformat-61.dll
#     21.1 MB. So the Linux note still applies — the port only ever decodes
#     MPEG-1 cutscenes, and a minimally-configured ffmpeg would cut this
#     dramatically. It is simply concentrated in one file instead of scattered.
#   * There is no system-integration hazard. On Linux, bundling libX11/libGL
#     would have been actively harmful because they must match the host's
#     display server and drivers. The Windows equivalents (d2d1, DWrite, USP10,
#     ntdll, msvcrt) are all *system* DLLs we deliberately do not ship, and the
#     graphics stack is reached through them, so the same rule falls out
#     naturally rather than needing a judgement call.
#
# libwinpthread-1.dll comes from the toolchain, not from Windows, so it IS
# bundled — the counterpart of the Linux bundle's decision to leave glibc and
# libstdc++ on the host. libgcc and libstdc++ are already statically linked
# (see the toolchain file), which is why they do not appear.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEPS="${THEOC_WIN_DEPS:-$ROOT/port/deps-win}"
BUILD="$ROOT/port/build-win"
OUT="$ROOT/dist/theoc-windows-x64"
TOOLCHAIN_FILE="cmake/toolchain-mingw-w64.cmake"

command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 || {
  echo "error: x86_64-w64-mingw32-g++ not found. brew install mingw-w64" >&2
  exit 1
}
[ -d "$DEPS/include/SDL2" ] || {
  echo "error: dependencies not staged at $DEPS" >&2
  echo "       see docs/porting/other-os-ports.md, 'Staging the dependencies'" >&2
  exit 1
}

echo ">>> configuring"
cmake -S "$ROOT/port" -B "$BUILD" \
      -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
      -DTHEOC_WIN_DEPS="$DEPS" \
      -DCMAKE_BUILD_TYPE=Release >/dev/null

echo ">>> building"
cmake --build "$BUILD" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null

[ -f "$BUILD/theoc.exe" ] || { echo "error: theoc.exe was not produced" >&2; exit 1; }

echo ">>> resolving the DLL closure"
rm -rf "$OUT"
mkdir -p "$OUT/bin"
# Strip: 14.1 MB -> 2.4 MB, and the symbols are regenerable by rebuilding. The
# import table survives, so the closure walk below still works on the stripped
# binary — but it is walked from the *build* output anyway, to keep the two
# independent.
x86_64-w64-mingw32-strip -o "$OUT/bin/theoc.exe" "$BUILD/theoc.exe"

# Anything matching this is part of Windows itself. Case-insensitive; the
# api-ms-win-* set is the Universal CRT, present on Windows 10 and later.
SYSTEM_RE='^(kernel32|kernelbase|user32|advapi32|ws2_32|gdi32|gdiplus|ole32|oleaut32|shell32|shlwapi|winmm|imm32|version|setupapi|bcrypt|ncrypt|secur32|crypt32|psapi|iphlpapi|userenv|uxtheme|dwmapi|shcore|rpcrt4|wintrust|ntdll|msvcrt|usp10|d2d1|dwrite|d3d9|d3d11|d3d12|dxgi|dxva2|opengl32|glu32|mf|mfplat|mfreadwrite|mfuuid|evr|avrt|winspool|comdlg32|comctl32|oleacc|hid|cfgmgr32|powrprof|api-ms-win.*)\.dll$'

# Where a non-system DLL might live: the staged prefix, then the toolchain
# (libwinpthread-1.dll and friends).
SEARCH="$DEPS/bin
$DEPS/lib
/opt/homebrew/Cellar/mingw-w64/*/toolchain-x86_64/x86_64-w64-mingw32/bin
/usr/lib/gcc/x86_64-w64-mingw32
/usr/x86_64-w64-mingw32/bin"

find_dll() {
  _name=$1
  for _dir in $SEARCH; do
    for _cand in $_dir/$_name $_dir/$(echo "$_name" | tr 'A-Z' 'a-z'); do
      [ -f "$_cand" ] && { echo "$_cand"; return 0; }
    done
  done
  return 1
}

# Breadth-first over imports. `pending` is the worklist, `seen` the closure.
pending=$(x86_64-w64-mingw32-objdump -p "$BUILD/theoc.exe" | sed -n 's/.*DLL Name: //p')
seen=""
missing=""
while [ -n "$pending" ]; do
  next=""
  for dll in $pending; do
    lower=$(echo "$dll" | tr 'A-Z' 'a-z')
    case " $seen " in *" $lower "*) continue ;; esac
    seen="$seen $lower"
    echo "$lower" | grep -qiE "$SYSTEM_RE" && continue      # ships with Windows
    if path=$(find_dll "$dll"); then
      cp -f "$path" "$OUT/bin/"
      echo "    bundled $dll"
      next="$next $(x86_64-w64-mingw32-objdump -p "$path" | sed -n 's/.*DLL Name: //p')"
    else
      missing="$missing $dll"
    fi
  done
  pending=$next
done

# Never fail silently on a dependency we could not find: a missing DLL is a
# "the application was unable to start correctly (0xc000007b)" dialog on the
# target, which says nothing useful about which one.
if [ -n "$missing" ]; then
  echo "error: could not locate these non-system DLLs:$missing" >&2
  echo "       the bundle would not start on a clean Windows machine" >&2
  exit 1
fi

# The timing probe ships with the bundle. It is standalone and statically
# linked, so it can be run on the target before theoc.exe is — which is the
# intended order, since it measures the one risk that decides whether the frame
# model works on this host at all.
echo ">>> building the timing probe"
x86_64-w64-mingw32-g++ -O2 -std=c++17 -static -Wall -Wextra \
    -o "$OUT/win-timing-probe.exe" "$ROOT/tools/win_timing_probe.cpp" -lwinmm
x86_64-w64-mingw32-strip "$OUT/win-timing-probe.exe"

cat >"$OUT/theoc.bat" <<'BAT'
@echo off
rem Launcher: run the binary from its own directory so the bundled DLLs beside
rem it are found, while keeping the working directory the user started in (the
rem game data is resolved relative to that).
setlocal
set "THEOC_HERE=%~dp0"
"%THEOC_HERE%bin\theoc.exe" %*
BAT

cat >"$OUT/README.txt" <<'TXT'
Theocracy — macOS/Linux/Windows port (Windows x86-64 build)
===========================================================

Run theoc.bat.

WHAT THIS IS
    The original 1999 Linux i386 binaries — theocracy.real and libmvos.so —
    executed under Unicorn, with only the OS/library ABI emulated natively.
    It is not a reimplementation and not the Windows build of the game.

WHAT YOU MUST SUPPLY
    The game data is not included and is not redistributable. You need:
      data/cd/linux/   theocracy.real, libmvos.so.0.9, server
      data/game/       the extracted CD data tree
    Point at them with THEOC_DATA and THEOC_CD if they are elsewhere.

STATUS ON WINDOWS
    This build has been produced by cross-compiling and has NOT been verified
    on Windows. The known risk is timing: the frame model needs sub-millisecond
    sleeps, and Windows' default scheduler granularity is ~15.6 ms. If pacing
    looks wrong, that is the first thing to measure — see win-timing-probe.exe
    and docs/porting/other-os-ports.md.

DIAGNOSTICS
    Every knob is an environment variable named THEOC_*. Start with:
      set THEOC_FPS=1        frame/heartbeat/sleep instrumentation
      set THEOC_WATCHDOG=1   says whether the guest is spinning or the host is
    The full catalogue is docs/porting/diagnostics.md.
TXT

SIZE=$(du -sh "$OUT" | cut -f1)
echo ">>> done: $OUT ($SIZE)"
echo
echo "    NOT verified on Windows — cross-built only. See README.txt."
