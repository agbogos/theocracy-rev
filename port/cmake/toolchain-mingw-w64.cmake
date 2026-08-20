# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
# CMake toolchain: cross-compile the port for Windows x86-64 from macOS or Linux.
#
#   brew install mingw-w64          # or: apt install mingw-w64
#   cmake -S port -B port/build-win \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
#         -DTHEOC_WIN_DEPS=$PWD/port/deps-win
#   cmake --build port/build-win
#
# WHY CROSS-COMPILING IS RIGHT HERE, HAVING BEEN WRONG FOR LINUX
# --------------------------------------------------------------
# docs/porting/other-os-ports.md rejects cross-compiling — but read the actual
# argument it makes: "the compiler is not the problem; the *sysroot* is", i.e.
# you would have to assemble Linux headers and shared objects for SDL2, Unicorn
# and libav plus their transitive dependencies by hand. That premise does not
# hold for Windows, which inverts it:
#
#   SDL2   ships an official MinGW development tarball (SDL2-devel-*-mingw)
#   ffmpeg has prebuilt Windows dev packages carrying headers + import libs
#   Unicorn is the only one that must be cross-built, and it is CMake with
#          upstream MinGW support
#
# Two of three are download-and-untar. Linux never developed that culture
# because distro packages made it unnecessary — which is exactly why the sysroot
# argument bit there and does not bite here. Same reasoning, opposite
# conclusion, because the fact it depended on changed.
#
# STAGING THE DEPENDENCIES
# ------------------------
# THEOC_WIN_DEPS points at one prefix holding the usual layout:
#
#   deps-win/include/{SDL2,unicorn,libavformat,...}/...
#   deps-win/lib/{libSDL2.a|libSDL2.dll.a, libunicorn.a, libavformat.dll.a, ...}
#   deps-win/bin/*.dll        <- what the eventual bundle ships alongside theoc
#
# Keep it inside the repo (port/deps-win/ is gitignored territory — it holds
# third-party binaries and must not be committed, same rule as data/cd/).
#
# STATUS: this file is scaffolding, and configuring with it will not produce a
# working theoc.exe today. port/src/traps.cpp is POSIX throughout — sockets,
# open/read/close, opendir, gettimeofday, usleep. That is the *point*: the first
# real deliverable of the Windows work is the error inventory this produces, the
# same way Linux's was "two compile errors, both BSD-isms". Harvest it before
# designing anything.

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

# Where the staged Windows dependencies live. Passed on the command line; the
# default keeps it inside the repo so nothing is written outside (CLAUDE.md).
set(THEOC_WIN_DEPS "${CMAKE_CURRENT_LIST_DIR}/../deps-win"
    CACHE PATH "Prefix holding staged Windows SDL2/Unicorn/ffmpeg")

# A minimal ffmpeg (tools/build-ffmpeg-min.sh) lives in its own prefix and must
# be inside the find root, or CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY below would
# reject it however it was hinted. Listed first so it wins over the staged
# full-fat ffmpeg in deps-win.
set(THEOC_FFMPEG_PREFIX "" CACHE PATH "Prefix of a minimal ffmpeg, searched first")
set(CMAKE_FIND_ROOT_PATH "${THEOC_FFMPEG_PREFIX}" "${THEOC_WIN_DEPS}")
set(CMAKE_PREFIX_PATH    "${THEOC_FFMPEG_PREFIX}" "${THEOC_WIN_DEPS}")

# Look for the host's own tools, but never for its libraries or headers —
# without this, find_library would happily hand a cross build /opt/homebrew's
# Mach-O libSDL2.dylib and fail confusingly at link time instead of clearly at
# configure time.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# port/CMakeLists.txt defaults THEOC_PREFIX to /opt/homebrew when APPLE. Cross
# builds are not APPLE (CMAKE_SYSTEM_NAME is Windows), so it defaults to empty
# and the find root above is the only search path — but pin it explicitly so a
# stale cache from a native build cannot leak a macOS prefix into a cross one.
set(THEOC_PREFIX "" CACHE PATH "Extra search prefix for dependencies" FORCE)

# Static libstdc++/libgcc so the result does not need the MinGW runtime DLLs
# beside it. The bundle still ships SDL2/ffmpeg/Unicorn DLLs; this just keeps
# the toolchain's own runtime out of that list.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++")
