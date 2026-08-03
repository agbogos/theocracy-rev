# Other-OS ports — Windows and Linux

**Status: not started.** The port is macOS / Apple Silicon only, and finished as
a port. This is the structure of the next two, written 2026-08-03 from an audit
of `port/src` rather than from general porting lore.

## The reusable core is bigger than it looks

`port/src` contains **zero platform conditionals** — no `__APPLE__`, no
`_WIN32`, nothing. That is a clean starting point, but it also means every
macOS assumption in there is implicit and has to be found rather than read off.

Four of the seven units already travel unchanged:

| Unit | Why it is portable |
|---|---|
| `guestlink.cpp` | the dual-image ELF32 linker — pure byte manipulation, no OS calls |
| `machine.cpp` | Unicorn 2 wrapper; Unicorn is cross-platform |
| `blit.cpp` | pixel math over guest memory |
| `mpeg.cpp` / `video.cpp` | libav + SDL2, both cross-platform |

**All of `docs/` transfers untouched.** The 232-symbol boundary, every struct
layout, every guest patch and every address describes a 1999 Linux i386 binary,
which does not care what is running it. So does every fix this year produced —
the save-file collapse, the cursor refresh, the sleep model, `THEOC_PROVINCE_MS`.

## Three things that look like blockers and are not

Worth killing early, because they would otherwise dominate planning:

- **`fork` and `execlp` are stubbed to `return 0`** (`traps.cpp`, the sem/signal
  stub list). `cTask` is inert and nothing misses it, so Windows' lack of `fork`
  costs nothing.
- **Exactly one real host thread** — the watchdog, a `std::thread`. The sound
  mixer and the timer are green-run *inside* the emulation
  ([host-architecture.md](host-architecture.md), "The green run"), so there is no
  host threading model to port.
- **No host `mmap`.** Unicorn owns guest memory.

## Linux is a subtraction, not a port

The host currently implements **the Linux ABI on top of BSD**. On a Linux host
those translations become the identity function — and they are not behind
conditionals, they are unconditional code in `traps.cpp` ("guest(Linux/i386) <->
host(BSD/macOS) socket translation"):

- `sockaddr_in` layout — BSD has `u8 sin_len` at offset 0, Linux a `u16` family
- `O_NONBLOCK` `0x800`→`0x0004`, `SOL_SOCKET` `1`→`0xffff`, `SO_REUSEADDR` `2`→`4`
- `to_linux_errno()` — EAGAIN 11/35, EINPROGRESS 115/36, EADDRINUSE 98/48

**Left in place on Linux these are actively wrong**, not merely redundant:
`to_linux_errno` would take an already-Linux errno and map it through a
BSD→Linux table. The Linux work is finding and neutralising translations, not
writing new ones.

One must **stay**, and is easy to get wrong: `__xstat` writes an **88-byte
Linux/i386** `struct stat`. A Linux *x86-64* host's own `stat` is a different
shape. The guest's ABI is i386 Linux, not "Linux" — and getting this wrong once
already cost three wrong diagnoses (re-methodology §7).

**Linux also gives an oracle nothing else does.** On x86-64 with 32-bit
libraries you can run `theocracy.real` and the real `libmvos.so` **natively,
unemulated**, beside the port. Every behavioural question this project has had to
answer by reverse-engineering becomes a differential test.

## Windows is the real port

Roughly in risk order:

1. **Timing precision — the biggest technical risk.** The frame model now rests
   on sub-millisecond sleeps: the re-entrant `usleep` splits an 83 ms request
   into ~3.5 tick-delivering slices ([frame-timing.md](frame-timing.md)).
   Windows' default scheduler granularity is ~15.6 ms without `timeBeginPeriod`,
   which would shred that. This needs designing, not translating.
2. **Sockets → Winsock2** — `closesocket`, `WSAGetLastError`, `ioctlsocket` for
   non-blocking, different `fd_set` semantics. `to_linux_errno` gains a second,
   larger sibling.
3. **Filesystem** — `open`/`stat`/`opendir`/`readdir`/`mkdir`, plus path
   separators inside guest-supplied paths.
4. **Clock** — `gettimeofday` → `QueryPerformanceCounter`.
5. **`THEOC_WATCHDOG_SAMPLE`** shells out to the macOS `sample` tool via
   `std::system`. No equivalent; stub it or drop it.

Audio needs nothing — `/dev/dsp` is already HLE'd onto SDL.

## Which binary does the Windows port run?

The CD ships a Windows build as well, so there is a real choice: keep emulating
the **Linux** binary as macOS does, or target the **Windows** one. The evidence
points hard at the first.

**Targeting the Windows binary discards the entire project.** Every address,
struct layout, vtable offset and patch in `docs/` was derived against
`theocracy.real` and `libmvos.so`. A PE build is a different compilation of a
different codebase: `guestlink.cpp` would need a PE/COFF sibling, and none of the
year's findings — the save-format fix, the frame limiter at `0x81da52a`, the GD
vtable slots — would carry.

**The Windows build also would not run as shipped.** `tex.pck` holds `Setup.exe`
and `theocracy-*.exe/.icd`, and the CD root carries `secdrv.sys`, `clokspl.exe`,
`clcd16.dll`, `clcd32.dll` and `drvmgt.dll` — a disc-based copy-protection
support set whose kernel driver **Microsoft disabled on Windows 10 and later**
(MS15-097). Whatever else it is, it is not a usable starting point on a modern
machine. *(Inferred from file names and the support-file set; `tex.pck` has not
been extracted or inspected.)*

Note the asymmetry that made this project possible at all: **the Linux binaries
ship unprotected.** That is why `theocracy.real` and `libmvos.so` could be read,
linked and run directly, and it is a large part of why the macOS port exists.

**And obtaining a usable Windows executable would not rescue the option**, which
is the part worth recording, because it is the argument someone will otherwise
re-run. The output would still be a *different compilation of a different
codebase*: none of this repo's addresses apply to it, so the reverse-engineering
starts from zero. And every improvement worth shipping — the save-corruption fix,
the 30 Hz cursor, sharp-bilinear presentation, `THEOC_PROVINCE_MS` — exists only
as work against the Linux binaries. A native Windows build would be the original
game with its original defects, including the one that ends a campaign at 51
saves.

**So: run the Linux binary on Windows.** One guest ABI across all three hosts,
one set of RE, one set of fixes. The Windows work stays what the section above
describes — a host-side OS shim — which is the whole reason this architecture was
chosen ([guest-libmvos.md](guest-libmvos.md)).

## The development environment: containers, not a VM

Initial Linux work needs **no VM, no target machine and no SSH**. A
`linux/arm64` container builds at native speed on Apple Silicon and can run the
headless verification too, because the port's diagnostics are text
(`THEOC_FPS`, the trap report, `THEOC_WATCHDOG`) and its pixel checks are BMP
dumps (`SDL_VIDEODRIVER=dummy` + `THEOC_SHOT_EVERY`/`THEOC_SHOT_DIR`) — the same
technique that verified sharp-bilinear on macOS with no display
([upscale-filtering.md](upscale-filtering.md)).

`Dockerfile` at the repo root is that environment. It bind-mounts the repo
rather than copying it, and builds into `port/build-linux` so the two platforms
do not fight over `CMakeCache.txt`.

Bring up a real Linux machine when — and only when — you need what the container
cannot give: interactive play, or the x86-64 oracle.

**Cross-compiling from macOS is the wrong tool here.** The compiler is not the
problem; the *sysroot* is — you would have to assemble Linux headers and shared
objects for SDL2, Unicorn and libav plus their transitive dependencies by hand.
A container is native compilation with the reproducibility you actually wanted.

**Do not forward X11 for timing work.** Under `ssh -X` the *client* renders, so
every present round-trips and the frame rate you measure is a property of the
network. On a project whose whole debugging history is wall-clock bugs
masquerading as performance bugs, that produces plausible wrong numbers. Use VNC
into a VM if you need to watch it, and keep judgement calls on real hardware.

## First Linux build — 2026-08-03

It builds and runs, and the prediction above held almost exactly: **two
compile errors, both BSD-isms, nothing else.**

- `sockaddr_in::sin_len` — a BSD-only leading length byte.
- `SO_NOSIGPIPE` — per-socket SIGPIPE suppression; Linux uses `MSG_NOSIGNAL`
  per send. Skipped there, because libmvos `main()`'s first act is
  `signal(SIGPIPE, SIG_IGN)` and the host honours it.

Both are now narrow `#if` guards behind `THEOC_HAVE_SIN_LEN` /
`THEOC_HAVE_SO_NOSIGPIPE` in `traps.cpp` — deliberately *not* a `platform/`
layer, because two sites do not earn an interface. That calculus changes at
Winsock.

Everything else compiled unchanged, and the boot is indistinguishable from
macOS: 79 COPY relocs, 34,994 relocs, **0 zero GOT/PLT slots**, 10/10 libmvos
and 215/215 game constructors clean, **0 faults, 0 aborts, 0 unimplemented
traps**. The menu renders correctly — verified from a BMP dump, headless, with
`SDL_VIDEODRIVER=dummy` and no X server anywhere:

```
[fps] 42.6 fps | guest 0.2M blk/s | heartbeat 25/s mixer 10/s
      sleep 0ms/s in 0 usleep | audio q=0.11s underrun=0/s | heap 2.6MB live
```

`sleep 0ms/s in 0 usleep` at the menu is expected — like `RealmGameLoop`, the
menu has no frame limiter of its own, so the 60fps ceiling is what bounds it.

**Not yet exercised on Linux:** anything past the menu (the province limiter and
the re-entrant sleep it drives), sockets and multiplayer — which is where the
remaining translation asymmetries live and where the interesting failures should
be — save/load, and interactive input.

## Sequencing

1. **Linux first.** It is mostly subtraction, it forces the platform seam into
   existence against the easier target, and because it is the guest's *native*
   ABI any residual translation bug surfaces immediately instead of hiding behind
   a second translation.
2. **Then Windows**, with the seam already proven by two implementations.

**Introduce `port/src/platform/` rather than sprinkling `#ifdef`s.** The
macOS-isms are concentrated — sockets, sleep, clock, filesystem, one shell-out —
so a thin interface with three implementations keeps `traps.cpp` readable.
Scattering conditionals through a 4000-line file is how this stops being
maintainable.

Also waiting, already known: **teardown**. `CloseSubsystems` is deliberately
skipped because process exit on macOS reclaims everything it would release, and a
platform where that is less true is the stated revisit trigger — see
[host-architecture.md](host-architecture.md), "Why teardown skips
`CloseSubsystems`".
