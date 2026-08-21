<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright (C) 2026 Adam Bogos
-->

# Third-party components

This project's own source is **GPL-2.0-or-later** (see [`LICENSE`](LICENSE)).
The released bundles also contain third-party libraries, unmodified, and this
file states what they are and how to obtain their source.

**The authoritative list for any given build is `THIRD-PARTY.txt` inside the
bundle itself.** It is generated at package time by
[`tools/third-party.sh`](tools/third-party.sh) from the bundle's actual
contents and the packaging machine's package database, so it records the exact
versions that shipped rather than the versions someone remembered. This file is
the policy; that file is the evidence.

Nothing here covers the game. Theocracy's data and its original binaries are
not included in any bundle, are not redistributable, and must be supplied from
your own copy.

## What ships, and under what

| Component | Licence | Source obligation |
|---|---|---|
| **Unicorn** | GPL-2.0-only **AND** GPL-2.0-or-later | **Yes** |
| **ffmpeg** (`libav*`, `libsw*`) | LGPL-2.1-or-later | **Yes** |
| **SDL2**, **SDL3**, **sdl2-compat** | zlib | No — attribution only |
| Linux only: X11, Wayland, ALSA, PulseAudio, dbus, systemd, codecs, … | MIT / LGPL-2.1+ / BSD, per package | Varies; see below |
| Windows only: `libwinpthread-1.dll` | MIT / BSD (mingw-w64 runtime) | No |

Unicorn is why this project cannot be licensed as anything later than
GPL-2.0-or-later: it carries QEMU-derived files that are GPL-2.0-**only**, and
those are incompatible with GPLv3 and AGPLv3. That constraint is inherited, not
chosen.

**ffmpeg is built here, and built as LGPL.** The configure line is in
[`tools/build-ffmpeg-min.sh`](tools/build-ffmpeg-min.sh) and carries neither
`--enable-gpl` nor `--enable-nonfree`, so no GPL-only codec is present. It is
cut down to the MPEG-1/MP2 path the cutscenes actually use — that is a size
decision (94 MB of `avcodec` down to ~600 KB), but it is also why the LGPL
relinking right is straightforward here: the libraries are shared objects, the
build is reproducible from the script, and the port links against them
dynamically.

**The Unicorn version differs by platform**, because each takes it from where
that platform gets libraries: Debian's `unicorn-engine` package on Linux,
Homebrew's `unicorn` formula on macOS, and a pinned source build on Windows.
The per-bundle `THIRD-PARTY.txt` says which.

**On Windows, Unicorn is statically linked into `theoc.exe`** rather than
shipped as a DLL, as are libgcc and libstdc++. Listing only the files in the
bundle would therefore understate what it contains, so the Windows manifest
names the statically linked components separately. The obligation is unchanged
— Unicorn is GPL either way — it simply has no file of its own to point at.
libgcc and libstdc++ are GPL-3.0-or-later **with** the GCC Runtime Library
Exception, which is what permits linking them into a GPLv2 binary at all.

## Obtaining the corresponding source

For any third-party component in any released bundle, **the corresponding
source is available on request for at least three years from the date of that
build.** Open an issue on this repository and say which bundle — the version is
in its directory name, in `THIRD-PARTY.txt`, and printed as the first line of
every run.

You do not have to wait for us, and for most components it is faster not to:

- **Linux.** Every library except ffmpeg is a stock Debian bookworm binary.
  `THIRD-PARTY.txt` lists the source package and exact version for each, and
  `apt-get source <source-package>=<version>` on a Debian system retrieves
  precisely what shipped.
- **macOS.** Every library except ffmpeg comes from the Homebrew formula named
  in `THIRD-PARTY.txt` at the version shown; the source is that project's
  upstream release for that version.
- **Windows.** Unicorn and ffmpeg are built from upstream source at the
  versions pinned in [`tools/stage-win-deps.sh`](tools/stage-win-deps.sh) and
  [`tools/build-ffmpeg-min.sh`](tools/build-ffmpeg-min.sh). SDL2 is the
  official mingw development release at the pinned version.

## Modifications

**There are none.** No bundled library is patched, forked or otherwise altered;
they are linked against, and shipped as built by Debian, Homebrew, or the
scripts in `tools/`. The port's own source is this repository, and the commit it
was built from is stamped into the binary, printed as the first line of every
run, and written into every save file.
