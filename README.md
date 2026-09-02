# Theocracy

Theocracy (Philos Laboratories, 2000) port to run netively on modern macOS,
Linux, and Windows.

The game's own i386 code is not rewritten. Both `theocracy.real` and the real
`libmvos.so` execute as the original 2000 binaries under Unicorn, and the host
in `port/` emulates only the OS and library boundary they sit on — libc,
pthreads, sockets, the display and audio devices. Single-player and multiplayer
run end to end on all three platforms, tested and verified.

The second half of the project is [`docs/`](docs/README.md): a reverse
engineering write-up of both binaries, which is meant to outlast the port.

## Running the port

### Quickstart

1. Grab the latest release appropriate for your system from the releases page,
  unpack it to a location you have full access to
2. Copy your CD's contents into `data/cd/` inside the port's folder
3. Copy the installed game files into `data/game/` OR use the provided python tool to extract it:
```sh
python3 tools/phls_extract.py data/cd/tdat.pck data/game
```
5. Make sure your `theoc.cfg` is configured right:
```
THEOC_DATA        = data/game    # installed game data
THEOC_CD          = data/cd      # the CD contents
```
6. Run it with `./theoc`

### Note

No game code or data is in this repository. Running it needs the Linux release's
binaries in `data/cd/` and the CD data extracted to `data/game/`, neither of
which is distributed here. The Linux binaries shipped on CD unprotected, which
is why they are the ones the port runs on every platform.

Theocracy was published by Ubi Soft, never re-released, and is on no storefront.
[`docs/README.md`](docs/README.md) records what is known about who holds the
rights.

## Build and run

macOS
```sh
brew install unicorn sdl2 ffmpeg cmake # one-time
cmake -S port -B port/build && cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc
```

The build is platform-neutral — no hardcoded prefix, and `-DTHEOC_PREFIX=…`
overrides the search root. Linux builds in the container described by
[`Dockerfile`](Dockerfile). Release bundles are cut by
`tools/package-{linux,macos,windows}.sh` into `dist/`.

Every runtime knob is a `THEOC_*` environment variable, catalogued in
[`docs/porting/diagnostics.md`](docs/porting/diagnostics.md). For anything that
looks like a hang, start with `THEOC_WATCHDOG=1`.

## Where to read

[`docs/README.md`](docs/README.md) is the index and says where the project
stands. The three worth knowing about:

- [`docs/porting/guest-libmvos.md`](docs/porting/guest-libmvos.md) — the
  architecture, and how it got there.
- [`docs/porting/host-architecture.md`](docs/porting/host-architecture.md) —
  what each unit of `port/src` owns.
- [`docs/reference/re-methodology.md`](docs/reference/re-methodology.md) — how
  to read these binaries without repeating our mistakes.

Open tasks are in [`todo.md`](todo.md).

## Licence

Our own code is `GPL-2.0-or-later` ([`LICENSE`](LICENSE)); the choice is forced
by Unicorn, which carries QEMU-derived GPL-2.0-only files. Third-party
components and the written offer for source are in
[`THIRD-PARTY.md`](THIRD-PARTY.md).

The generated symbol and signature tables (`data/*.tsv`, `data/mvos_api.json`,
`include/mvos_api.hpp`) carry no copyright header: they are derived from the
game binary and are interface facts, not our expression.

# Contributing

The author is not looking for contributors at this stage, and considers the
port complete.

While the effort will be appreciated, any and all PRs will be closed without
review. Visitors are welcome to fork the project to extend it as long the license
is honored.

If you encounter an issue with the port, please open an Issue on GitHub.
