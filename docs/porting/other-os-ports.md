# Other-OS ports — Windows and Linux

**Status: Linux done and played. Windows builds but has never run.** Both were
brought up on 2026-08-03. Linux is confirmed by play — see
[Confirmed by play](#confirmed-by-play).

Windows now **compiles, links and packages** to
`dist/theoc-windows-x64/` via `tools/package-windows.sh`, cross-built from macOS
with mingw-w64 and no Windows machine in the loop. That is the whole claim: **not
one instruction has executed on Windows**, so nothing here says the game works.
The audited risk list below has measurements against it now — they moved three
of its five items — and the one risk that survived, sub-millisecond sleep, is
still unmeasured because measuring it needs a Windows host. See
[What the cross compiler says](#what-the-cross-compiler-says) and
[The Windows build](#the-windows-build--2026-08-03).

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

> **Correction (2026-08-03, measured).** This section originally claimed those
> translations would be *actively wrong* on Linux — that `to_linux_errno` would
> map an already-Linux errno through a BSD→Linux table. **That was wrong, and it
> was an inference rather than a measurement.** The translations are written
> *host macro → guest constant*: `case EAGAIN: return 11`. On Linux the host
> macro **is** the guest constant, so every one of them is the identity;
> on macOS every one translates. Verified by compiling the same probe on both:
>
> ```
> Linux:  EAGAIN host=11  table=11   … 0 of 12 differ   O_NONBLOCK=0x800 SOL_SOCKET=1 SO_REUSEADDR=2
> macOS:  EAGAIN host=35  table=11   … 12 of 12 differ  O_NONBLOCK=0x4   SOL_SOCKET=65535 SO_REUSEADDR=4
> ```
>
> So there is **nothing to neutralise**: the socket layer was already portable by
> construction, and the only things that actually broke were the two BSD-isms the
> first build found. The general lesson is this project's own —
> [re-methodology](../reference/re-methodology.md) — *when a claim is about
> observable state, observe it.* A one-file probe settled in a minute what a
> plausible-sounding argument got backwards.

One must **stay**, and is easy to get wrong: `__xstat` writes an **88-byte
Linux/i386** `struct stat`. A Linux *x86-64* host's own `stat` is a different
shape. The guest's ABI is i386 Linux, not "Linux" — and getting this wrong once
already cost three wrong diagnoses (re-methodology §7).

**Linux also gives an oracle nothing else does.** On x86-64 with 32-bit
libraries you can run `theocracy.real` and the real `libmvos.so` **natively,
unemulated**, beside the port. Every behavioural question this project has had to
answer by reverse-engineering becomes a differential test.

## Windows is the real port

> **Measured 2026-08-03, and it moves three of the five items below.** The list
> that follows was written from an audit, before anything had been compiled. A
> mingw-w64 cross toolchain and an afternoon then produced actual numbers; see
> [What the cross compiler says](#what-the-cross-compiler-says) immediately
> after it. Read both — the list is kept as written so the corrections have
> something to correct, which is this repo's habit.

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

## What the cross compiler says

**Cross-compiling is the right tool for Windows, having been the wrong one for
Linux.** The rejection above is about the *sysroot* — "the compiler is not the
problem" — and Windows inverts that premise: SDL2 ships an official MinGW
development tarball and ffmpeg has prebuilt Windows dev packages, so two of the
three dependencies are download-and-untar and only Unicorn must be cross-built.
Linux never developed that culture because distro packages made it unnecessary.
Same argument, opposite conclusion, because the fact it rested on changed.

So: `brew install mingw-w64`, `port/cmake/toolchain-mingw-w64.cmake`, and no
Windows machine anywhere in the loop.

### Four of seven units cross-compile unmodified — and it is a different four

A `-fsyntax-only` pass with `x86_64-w64-mingw32-g++ -std=c++17`:

| Unit | Result |
|---|---|
| `guestlink.cpp` | **clean** |
| `machine.cpp` | **clean** |
| `blit.cpp` | **clean** |
| `main.cpp` | **clean** — *not* on the predicted list |
| `video.cpp` / `mpeg.cpp` | untested — need Windows SDL2 / libav headers staged |
| `traps.cpp` | the whole port, see below |

The prediction was right in its count and wrong in its membership: `main.cpp`
travels, and `video.cpp`/`mpeg.cpp` are unproven rather than proven. They are
still the *likeliest* to travel — SDL2 and libav are genuinely cross-platform —
but "likely" is what the audit already said, and staging the headers is what
would turn it into a fact.

> **What this pass does and does not prove.** `-fsyntax-only` parses and
> type-checks; it does not codegen and does not link, so a unit can pass here and
> still fail on a missing symbol. It also used the *macOS* Unicorn headers, which
> is sound because `unicorn.h` is a portable C API but is not the same as
> building against a staged Windows one. Treat this as a strong indication and a
> work-list, not as a working build.

### The entire POSIX gap in `traps.cpp` is sockets

`traps.cpp` stops at its first missing header, so the useful question is which of
its POSIX includes mingw-w64 lacks. Tested one header per compile:

| Present in mingw-w64 | Missing |
|---|---|
| `csignal`, `dirent.h`, `fcntl.h`, `sys/stat.h`, `sys/time.h`, `unistd.h`, `pthread.h` | `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `netdb.h`, `sys/select.h` |

**Every missing header is a socket header.** And the present ones are not merely
present — `gettimeofday`, `usleep`, `opendir`/`readdir`, `open` with `O_BINARY`,
`fstat` and `close` all *link* in a static mingw build, which was checked
separately because a header existing is not a symbol existing.

That moves the risk list:

- **Risk 3, filesystem — largely dissolves.** `open`/`stat`/`opendir`/`readdir`
  are all there. What survives is *semantics*, and one specific hazard:
  **`O_BINARY`**. Windows translates CRLF on handles opened without it, which
  would silently corrupt the `.tsg` saves and the PHLS packs — a data-corruption
  bug that compiles cleanly and only shows up in a file. Every guest-facing
  `open` must carry it.
- **Risk 4, clock — dissolves.** `gettimeofday` links; no
  `QueryPerformanceCounter` translation needed for correctness.
- **pthreads — never a risk.** mingw ships winpthreads, so the watchdog thread is
  fine. (`fork`/`execlp` were already stubs.)
- **Risk 2, sockets — confirmed, and now the only structural port work.** It is
  also the point where `port/src/platform/` finally earns its keep, per
  [Sequencing](#sequencing).

### Risk 1 is untouched, and `usleep` linking makes it worse

`usleep` is in the "present and links" column — and that is the trap, not the
reprieve. It links; it says nothing about *granularity*. The port would build,
run, and be quietly wrong: mingw's `usleep` is a wrapper over the same Windows
sleep primitive whose default granularity is ~15.6 ms, and
[the sleep model](#windows-is-the-real-port) needs sub-millisecond slices.

Read the `usleep` handler in `traps.cpp` to see why sub-millisecond is not an
edge case. It does **not** sleep the guest's 83 ms; it sleeps in slices bounded
by the next 30 Hz heartbeat tick:

```
slice = min(remaining, time_until_next_tick);
```

so the requested duration is uniform over (0, 33.3 ms] and is **routinely
sub-millisecond**, on every frame, as the tail slice before a tick comes due. A
15.6 ms floor does not add jitter to those — it overshoots them by 15x–150x and
sails past the deadline the slice existed to stop at.

**So the one risk that cannot be dissolved by reading a header is the one that
was ranked first, and it is now the only deep unknown in the Windows port.**

`tools/win_timing_probe.cpp` measures it, standalone, before `traps.cpp` is
touched — the errno-probe move from [Linux is a subtraction](#linux-is-a-subtraction-not-a-port),
applied to the claim that is currently still an argument. It cross-compiles from
macOS with no Windows involved:

```sh
x86_64-w64-mingw32-g++ -O2 -std=c++17 -static -Wall -Wextra \
    -o win-timing-probe.exe tools/win_timing_probe.cpp -lwinmm
```

Three tests across four candidate implementations (`Sleep`, `Sleep` +
`timeBeginPeriod(1)`, `CreateWaitableTimerEx(HIGH_RESOLUTION)`, and that timer
plus `timeBeginPeriod`): a single-shot sweep weighted toward the sub-millisecond
rows that decide it, a sustained 30 Hz heartbeat measured as lateness against an
*absolute* schedule (which is how `timer_next_` advances, so overshoot
accumulates rather than averaging out), and the province frame sliced exactly as
the handler slices it, reported as ticks/frame against the ~3.5 the model needs.

Two things to do with it, both mattering more than the headline number:

- **Run it with `--busy N`.** The multiplayer session that prompted this ran
  three game instances on one host; scheduler granularity under contention is
  not the idle-box number.
- **Run it on the VM *and* on bare metal.** VM timer behaviour is not the host's,
  and that gap has to be known before any later in-VM measurement can be
  trusted — the same reason this doc refuses X11 forwarding for timing work.

**Status: written and cross-compiles clean; not yet run.** No numbers exist yet,
and nothing above should be read as predicting them. It ships inside the Windows
bundle so it can be run on the target *before* `theoc.exe` is.

## The Windows build — 2026-08-03

**`theoc.exe` builds, links and packages.** Nothing has run on Windows, so this
section is about what the compiler and linker settled, not about the game
working. Produced entirely by cross-compiling from macOS:

```sh
tools/package-windows.sh          # -> dist/theoc-windows-x64/
```

### Staging the dependencies took longer than the port

SDL2 and ffmpeg were download-and-untar as predicted. Unicorn had to be
cross-built, and failed twice for reasons worth recording because neither is
about Unicorn:

- **`qemu/configure` runs under `execute_process` with no error checking**, so
  its failure surfaced ~200 source files later as `config-target.h: No such file
  or directory`. The actual cause was a **missing `pkg-config` binary** — the
  same thing `port/CMakeLists.txt` already warns about for macOS, in its comment
  explaining why the port itself does not use pkg-config.
- `-DUNICORN_ARCH=x86 -DBUILD_SHARED_LIBS=OFF` is the useful configuration:
  x86-only cuts the build substantially, and static means one less bundled DLL.

### The port needed 11 fixes, and one of them was a real bug

`traps.cpp` reached zero errors in eleven changes. Ten were mechanical; these
are the ones that were not:

| Fix | Why it is more than mechanical |
|---|---|
| **`HostFile::sock`** | Winsock SOCKETs are a **separate namespace from CRT fds**, so `::read`/`::write`/`::close` on one are silently wrong. libmvos forces the issue: `cIPCO_TCPIP::Read/Write` poll the socket through plain `read`/`write`. This would have compiled, linked, and failed at runtime. |
| **`O_BINARY`** everywhere | Windows translates CRLF and stops at `0x1a` in text mode. The `.tsg` saves, the PHLS `.pck` archives and the MPEG cutscenes are all binary. A clean-compiling, silent data-corruption bug. `fopen` mode strings get a `'b'` forced on for the same reason — the guest is a Linux binary, so it never supplies one. |
| **`last_socket_errno()`** | Winsock errors come from `WSAGetLastError()` with 100xx numbering. libmvos maps anything outside errno 4..22 to its generic "unknown error", and a non-blocking read with no data is the common case *every netgame frame* — so leaking `WSAEWOULDBLOCK` (10035) instead of 11 would be the BSD `EAGAIN=35` bug again, three orders of magnitude further out of range. |
| **`fcntl` → `ioctlsocket(FIONBIO)`** | Windows has no `fcntl`, and `FIONBIO` is **write-only** — there is no way to read the blocking state back. `HostFile::nonblock` remembers it, which is exact only because this handler is its sole writer. |
| **`std::floor` in `video.cpp`** | A genuine latent bug, not a Windows-ism: `<cmath>` was never included and libc++/libstdc++ happened to provide it transitively. **Cross-compiling found a real defect in the working macOS build.** |

The rest were `char*` casts for Winsock, `socklen_t`→`int`, one-argument `mkdir`,
`localtime_r`→`localtime_s` (reversed arguments), absent `st_blksize`/`st_blocks`,
absent `dirent::d_type` (derived with `stat` instead), `in_addr_t`, and a
`SIGPIPE` guard — Windows has no SIGPIPE, so the guest's request to ignore it is
satisfied by the platform.

Link needed `ws2_32` and `winmm`, plus **`SDL_MAIN_HANDLED`**: on Windows,
including `SDL.h` `#define`s `main` to `SDL_main` and expects `SDL2main` to
supply a `WinMain`. The host wants its own `main`.

**The macOS build was rebuilt and is unaffected** — same warnings, exit 0. Every
change is either behind `#if defined(_WIN32)` or a no-op on POSIX (`O_BINARY` is
0, the `'b'` in an fopen mode is ignored, `HostFile::sock` is merely informative
where one namespace covers both).

### The bundle

`dist/theoc-windows-x64/` — launcher, `bin/theoc.exe`, seven DLLs, the timing
probe, README. The DLL list is **not hand-written**: it is the transitive import
closure of `theoc.exe` walked with `objdump` and filtered against a denylist of
DLLs that ship with Windows, because the Linux bundle already established that
guessing does not work and the loader is the oracle.

Two things measured rather than assumed:

- **119 MB**, of which `avcodec-61.dll` alone is 89.6 MB. The closure is seven
  DLLs against Linux's ~160-library graph, but that is **not** because it carries
  less — this ffmpeg links its codec dependencies *into* the av\* DLLs. The Linux
  note therefore still stands: the port only decodes MPEG-1 cutscenes, and a
  minimally-configured ffmpeg would cut this dramatically. (The first draft of
  the packaging script claimed "~50 MB" from reasoning rather than measurement,
  and was wrong by 2.5x.)
- **Stripping `theoc.exe` takes it from 14.1 MB to 2.4 MB**, and the symbols are
  regenerable by rebuilding.

There is no system-integration hazard to reason about, which on Linux was the
hard part: the Windows equivalents of libX11/libGL (`d2d1`, `DWrite`, `USP10`,
`ntdll`, `msvcrt`) are all system DLLs the denylist excludes anyway, so the rule
falls out instead of needing a judgement call. `libwinpthread-1.dll` **is**
bundled — it comes from the toolchain, not from Windows — while libgcc and
libstdc++ are statically linked and so never appear.

### What is not known

**Nothing has executed.** Not the boot, not a single trap, not one frame. The
things most likely to be wrong, in order:

1. **Timing** — the untouched risk 1. Run `win-timing-probe.exe` first.
2. **Path handling.** `resolve_path` builds `/`-separated paths and the guest
   supplies Unix ones. Windows APIs accept `/`, but nothing here has tested a
   drive-letter root, and `guest[0] == '/'` as the "absolute path" test is
   simply not how Windows spells that.
3. **`THEOC_WATCHDOG_SAMPLE`** shells out to the macOS `sample` tool via
   `std::system`. Compiles; will do nothing useful. Off by default.
4. **`CloseSubsystems` is still skipped** — and Windows is the platform
   [host-architecture.md](host-architecture.md) names as the revisit trigger for
   that decision.

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

### Province, driven headlessly — the first cross-platform differential

`THEOC_AUTO_PROVINCE=1` self-drives menu → Prophecy → OK into province, so the
frame limiter, the re-entrant sleep and `cProvince_Do` can all be exercised with
no display. Run on both platforms under `SDL_VIDEODRIVER=dummy`:

| | Linux (container, Unicorn 2.0) | macOS (native, Unicorn 2.1) |
|---|---|---|
| province fps | 9.3–9.9 | 10.2–10.5 |
| guest blocks/frame | 0.15M | 0.14–0.15M |
| guest blocks/s | 1.4–1.5M | 1.5M |
| heartbeat | 29–31/s | 30–31/s |
| `usleep` calls/s | 39–43 | 41–44 |
| guest heap live | 28.6 MB | 28.6 MB |
| audio underrun/s | 396–1502 | 0 |
| `THEOC_START_SEC` | **never fires** | fires |
| unimplemented traps | — | **0** |

**The port behaves identically where it matters.** Guest blocks per frame, blocks
per second and live heap all match, so the emulation is doing the same work;
`heartbeat ~30/s` with ~4 `usleep` calls per frame means the re-entrant sleep and
its tick-delivering splice work on Linux exactly as designed. Province renders
correctly (verified from a BMP dump).

Two differences, one of them real:

- **`THEOC_START_SEC` does not fire on Linux.** It is Unicorn's own
  `uc_emu_start` timeout (`Machine::call`). macOS logs `Start (host Start
  timeout — still in game)` and exits 0; Linux ran 3+ minutes past it. Debian
  ships **Unicorn 2.0** where Homebrew has **2.1**, which is the leading
  suspicion but is not proven. This is a *harness* knob, not game behaviour —
  work around it with `timeout -s KILL N` in the container.
- **Audio underruns, 400–1500/s against 0.** Both runs used SDL's dummy audio
  driver, which paces its callback differently per platform, so this is most
  likely an artifact of headless audio rather than a mixer defect. Worth
  re-checking against a real audio device before treating it as a bug.

> **Do not read the absolute fps as a port measurement.** These runs are
> unattended, on a fresh campaign start, and produce ~0.15M guest blocks/frame
> against the ~0.41M an interactive session shows — a different scene, and a
> different (possibly CPU-constrained) execution environment. What is comparable
> here is **Linux against macOS within the same conditions**, and the structural
> columns, not the frame rate.

### Sockets, exercised headlessly

`THEOC_SERVER=1` runs the shipped dedicated server, which is headless *by
derivation* — it carries no `_12cApplication.Video` flag, so no display is ever
brought up. That makes the whole socket layer testable in a container.

Boot is **identical to macOS line for line**: same entry addresses, `10 ok / 0
faulted` libmvos constructors, `Network 0 → 1`, `socket(type=1) -> guest fd 3`,
`bind(:5042) ok`. Connecting a real TCP client from inside the container then
gives:

```
[net] accept -> guest fd 4 from 127.0.0.1:52670
```

That single line validates both BSD-ism fixes and both directions of the address
translation: `bind` goes through `guest_to_host_sin` (the `sin_len` skip) and
`accept` writes the peer back through `host_to_guest_sin`. Sockets on Linux are
sound.

### The 20-cycle soak, both platforms — bit-identical

`THEOC_SOAK=20 THEOC_SOAK_PLAY=20` drives 20 full load/unload cycles (menu →
Prophecy → OK → province → map → exit → confirm → menu), ~9 min, headless.

**All 20 cycles are identical between macOS and Linux** — live heap *and*
frontier, to two decimals:

```
cycle  1 | heap 11.65 MB live / 28.74 MB frontier | esp 0x6ffff3e4 | stubs 144 B | fds 2
cycle 20 | heap 12.01 MB live / 29.74 MB frontier | esp 0x6ffff3e4 | stubs 144 B | fds 2
```

`0 faults, 0 aborts, 0 [slow] stalls` on both. Guest ESP is the same constant
across all 20 cycles on both hosts, so the green-thread mixer does not drift on
Linux either; the stub page stays flat at 144 B.

Two things follow, and the second is the one that mattered:

- **The guest heap is deterministic and host-independent.** Same allocation
  sequence, same totals, different kernel. That also sharpens
  [open question #30](../open_questions.md): the +18 KB/cycle is the *guest*
  allocating, not the port leaking.
- **The 2026-08-03 sleep/present/cursor rewrite did not perturb allocation at
  all** — these numbers reproduce the 2026-07-25 baseline exactly. That closes
  the "those changes have never been soaked" gap, on both platforms at once.

Two instrument findings fell out:

- **`fds` reads 2, not 1.** The 2026-07-25 note recorded 1; both hosts now say 2
  and stay flat, so the old figure is stale rather than the port having leaked a
  descriptor.
- **`rss` reads 0.0 MB inside the container.** The host-RSS probe does not work
  there, so host memory has to be watched from outside. On macOS it went
  156.7 → 155.8 MB over the run — *down*, and non-monotonic as documented.

### Confirmed by play

Everything the headless work could not reach was then **played on a real Linux
machine** (2026-08-03): interactive input, save/load, the province limiter and
the re-entrant sleep it drives, and a full netgame — one dedicated server and
two clients — which proves the *client* side that the container could only prove
from the server's accept path. All parts of the game were exercised and none
misbehaved.

*Provenance: this is the maintainer's report from an interactive session, not an
instrumented run. It is the right kind of evidence for "does it work" — a human
playing every part of the game is exactly what the headless harness cannot
substitute for — and the wrong kind for any number, which is why the paragraph
below refuses to produce one.*

**Multiplayer felt slower than macOS, and that observation is not usable.** The
session ran three game instances (server + two clients) inside a VM on a single
Gentoo host, so the comparison confounds at least four variables against the
macOS figures: virtualised execution, one host CPU shared three ways, a
completely different machine, and a workload the macOS side never ran. Nothing
here indicates a Linux-specific defect, and the structural evidence points the
other way — the 20-cycle soak is bit-identical across the two platforms, and
guest blocks per frame match, so the emulation is demonstrably doing the same
work per frame on both.

Recorded so the impression does not later get cited as a measurement. **What a
real answer would need:** one instance per host, the same scene on both
platforms, bare metal on each, and `THEOC_FPS`'s structural columns — guest
blocks/frame and blocks/s — rather than the frame rate, for the reason the
province differential already gives above.

## Packaging a Linux bundle

`tools/package-linux.sh [amd64|arm64]` builds and packages in one step, entirely
in a container so `ldd` sees the *target* architecture. Output is
`dist/theoc-linux-<arch>/` — a launcher, `bin/theoc`, `lib/`, and a README.
Verified self-contained by running it in a bare `debian:bookworm-slim` with no
development packages installed: 0 loader errors, 215/215 constructors, video up.

**What is bundled, and the mistake that decided it.** The first attempt used a
hand-written denylist that kept X11, GL, ALSA and PulseAudio on the host, on the
theory that these are system-integration libraries SDL `dlopen`s. The bundle
would not start at all: Debian's SDL2 has `libasound.so.2` as a hard
`DT_NEEDED`. **Guessing which dependencies are dlopen-ed does not work — the
loader is the oracle.**

The graph turns out to contain no `libGL`/`libEGL` at all, because SDL2 really
does load GL at runtime. So the one group that would be genuinely dangerous to
ship is absent by construction, and **the host GPU stack is always used no matter
what we bundle**. That leaves only client libraries speaking stable protocols to
host daemons, so the denylist shrank to just glibc and libstdc++/libgcc — the
same trade-off the Steam runtime makes. `THEOC_SYSTEM_LIBS=1` ignores the bundle
if it ever bites on an unusual driver stack.

Two consequences worth knowing:

- **~190 MB**, almost all of it ffmpeg's codec dependencies (x264, x265, vpx,
  theora, srt, zmq…). The port only ever decodes MPEG-1 cutscenes, so a
  minimally-configured ffmpeg would cut this dramatically — worth doing if the
  bundle is ever distributed rather than just tested.
- **glibc ≥ 2.36 and libstdc++ from GCC 12** on the target, inherited from the
  bookworm base. For an older target, build the image `FROM` an older base
  rather than bundling around it.

## Sequencing

1. ~~**Linux first.**~~ **Done 2026-08-03.** It is mostly subtraction, it forces
   the platform seam into existence against the easier target, and because it is
   the guest's *native* ABI any residual translation bug surfaces immediately
   instead of hiding behind a second translation.
2. **Then Windows**, ~~with the seam already proven by two implementations~~ —
   but see below: step 1 did not in fact produce a seam.

**Step 1's second reason did not hold, and Windows should not assume otherwise.**
Linux was expected to force `port/src/platform/` into existence; it needed *two
`#if` guards* and nothing more, because the socket layer turned out to be
portable by construction. So Windows arrives with **no seam already proven** —
it has to build the first one, against the harder target, which is the opposite
of the leverage this ordering was meant to buy. The ordering was still right for
its other two reasons (the native-ABI oracle, and subtraction being cheap), and
Linux is the reference implementation that makes any Windows divergence
diagnosable. But the estimate for Windows should not be discounted on the
strength of a seam that does not exist.

**Introduce `port/src/platform/` rather than sprinkling `#ifdef`s** — at Winsock,
which is where it finally earns its keep. The macOS-isms are concentrated —
sockets, sleep, clock, filesystem, one shell-out — so a thin interface with three
implementations keeps `traps.cpp` readable. Scattering conditionals through a
4000-line file is how this stops being maintainable. Two sites did not earn an
interface, which is why Linux correctly did not build one; Windows touches every
item on that list.

Also waiting, already known: **teardown**. `CloseSubsystems` is deliberately
skipped because process exit on macOS reclaims everything it would release, and a
platform where that is less true is the stated revisit trigger — see
[host-architecture.md](host-architecture.md), "Why teardown skips
`CloseSubsystems`".
