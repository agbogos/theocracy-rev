# Other-OS ports — Windows and Linux

**Status: both done and played.** Linux was brought up and confirmed by play on
2026-08-03, Windows on 2026-08-04 — see [Confirmed by play](#confirmed-by-play)
and [The game runs on Windows](#the-game-runs-on-windows--done-2026-08-04).
Three hosts now run the same 2000 i386 binaries from the same source, with no
`#ifdef` in any unit but `traps.cpp`.

Both bundles are cross-built from macOS with no machine of the target OS in the
loop: `tools/package-windows.sh` → `dist/theoc-windows-x64-<version>/` (mingw-w64),
`tools/package-linux.sh [amd64|arm64]` → `dist/theoc-linux-<arch>-<version>/`
(a container).

One item is outstanding: a **bare-metal Windows timing run**, blocked on hardware
until ~2026-08-18. It qualifies a measurement rather than blocking anything — see
[The contention runs](#the-contention-runs--2026-08-04).

> **This document is written forwards, and is kept that way on purpose.** The
> risk list below was authored from an audit *before anything was compiled*, and
> the corrections that follow it are left as corrections rather than folded back
> in. Three of its five items were moved by measurement, one was the real risk,
> and one prediction (that Windows would force a `port/src/platform/` directory)
> was simply wrong. Reading it in order is the point; reading only the top is
> what this status block is for.

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

It ships inside the Windows bundle so it can be run on the target *before*
`theoc.exe` is.

### The probe's answer — measured 2026-08-03, in a VM

Run in the Windows VM (12 logical CPUs, QPC at 10 MHz). **The risk is real, and
it has a clean fix.**

| requested | `Sleep()` | `Sleep()`+`timeBeginPeriod(1)` | waitable timer (HIGH_RESOLUTION) |
|---|---|---|---|
| 0.100 ms | **15.944** | 2.000 | **0.634** |
| 0.500 ms | **15.957** | 2.000 | **0.634** |
| 1.000 ms | **15.928** | 2.000 | 1.670 |
| 16.667 ms | 31.056 | 17.998 | 17.013 |
| 83.333 ms | 94.114 | 84.745 | 83.990 |

(medians, ms). Sustained 30 Hz heartbeat, median lateness against an absolute
schedule: `Sleep()` **9.04 ms**, `+timeBeginPeriod` 1.59 ms, waitable timer
**0.655 ms**. Province frame, sliced as `traps.cpp` slices it: **94.0 ms**,
84.9 ms, **84.0 ms** against an 83.3 ms target.

Four things fall out, and the third was not predicted:

1. **The 15.6 ms floor is exactly as feared.** A naive port would run the
   province frame ~13% slow, with the heartbeat 9 ms late every tick — and
   because the frame limiter is elapsed-based, that is a *game-speed* error, not
   just jitter.
2. **`CreateWaitableTimerEx(HIGH_RESOLUTION)` solves it.** 0.634 ms for a
   0.1 ms request, and an 84.0 ms province frame — 0.8% off target, which is
   better than `timeBeginPeriod` manages and close enough that nothing downstream
   would notice. This is the "contained change, not a redesign" branch the probe
   was written to distinguish.
3. **`timeBeginPeriod(1)` on top of the waitable timer buys nothing** (0.647 vs
   0.655 ms — noise). So the port needs the timer alone, and does not need to
   raise the system-wide timer resolution at all. That is worth having: raising
   it is a global side effect with a power cost, and this says to skip it.
4. **`NtQueryTimerResolution` reported the current resolution as 1.0 ms while
   `Sleep()` still took 15.9 ms.** Not a contradiction — since Windows 10 2004,
   `timeBeginPeriod` affects only the *calling process*, so the global figure
   says nothing about what this process gets. **A reading of
   `NtQueryTimerResolution` is not evidence about your own sleeps**, which is
   exactly the kind of plausible-but-wrong instrument this project keeps meeting.

> **Caveat, and it is not a small one: this was measured in a VM.** VM timer
> delivery is mediated by the hypervisor, so these numbers describe *this VM on
> this host*, not Windows. The qualitative result is safe — the ordering of the
> four methods, and the fact that the high-resolution timer tracks sub-millisecond
> requests where `Sleep()` cannot, are not the sort of thing virtualisation
> inverts. The absolute figures should not be quoted as "Windows does X". A
> bare-metal run is still wanted, and there is no bare-metal Windows available to
> this project.

### What shipped — 2026-08-04

`theoc_sleep_us()` in `traps.cpp`, in the platform block beside `theoc_mkdir()`,
replacing `::usleep` at its three call sites (the `THEOC_LEGACY_SLEEP` A/B path,
the tick-bounded slice loop that is the real one, and the `THEOC_FRAME_MS`
present-to-present cap). On POSIX it *is* `::usleep`; the Windows branch is the
waitable timer.

Three decisions inside it, each of which the probe's table settles:

- **One timer handle for the process, created on first use and never closed.**
  At ~40 slices/s, a create/close pair per sleep is a syscall round trip per
  slice buying nothing.
- **No `timeBeginPeriod` on the timer path** — row 3 of the four findings above.
  It is a system-wide side effect with a power cost and it measured as noise.
- **The fallback skips the coarse waitable timer** that `CreateWaitableTimerEx`
  degrades to when the flag is rejected (pre-Windows-10-1803), and goes straight
  to `timeBeginPeriod(1)` + `Sleep()`. A coarse waitable timer rides the same
  15.6 ms tick as `Sleep` and would buy nothing over it, where
  `timeBeginPeriod` measured 2.0 ms. The fallback announces itself on stderr —
  it is a 2%-slow province frame, which is exactly the kind of thing that gets
  misdiagnosed as a performance problem a year later.

It stayed **inline in `traps.cpp`**. There is still no `port/src/platform/`
directory and it has still not been earned: this is the second host difference
to want a home there and, like the Winsock work, it is one narrow `#if` block
next to the other one. Three would be an argument; two is a directory holding
two functions.

#### Measured on Windows — the fix lands, with a residual

A 6-minute instrumented run (`THEOC_FPS=1 THEOC_WATCHDOG=1 THEOC_LONGRUN=60`),
three minutes parked on realm and three on province. **No `[timing]` line**, so
the high-resolution timer was acquired and this is the intended path rather than
the fallback.

| | naive `Sleep` (probed) | shipped timer (measured in-game) | target |
|---|---|---|---|
| province frame | 94.0 ms | **87.0 ms** (11.5 fps) | 83.3 ms |
| error | +13% | **+4.4%** | — |
| realm | — | **60.0–60.4 fps** | 16 ms cap |

**Realm is the cleanest proof, and it is an accident of the frame cap.** That
cap is a `theoc_sleep_us()` call for a 16 ms interval; under `Sleep()`'s 15.6 ms
granularity a 16 ms request quantises to ~31 ms and the screen would sit at
~32 fps. Hitting 60 is only reachable with sub-millisecond resolution. It is not
the display, either: the renderer is created without `SDL_RENDERER_PRESENTVSYNC`
and samples land *above* 60 (60.3, 60.4), which a vsync clamp cannot produce.

**The residual 3.7 ms is the timer's own floor, not a defect**, and the log
decomposes it without needing another probe:

```
43.5 usleep/s ÷ 11.5 fps = 3.78 slices/frame      (macOS: 3.5 — splice identical)
740 ms/s      ÷ 11.5 fps = 64.3 ms/frame sleeping
87.0 − 64.3              = 22.7 ms/frame computing
so the guest asks for 83.3 − 22.7 = 60.6 ms and gets 64.3
3.7 ms overshoot ÷ 3.78 slices ≈ 1.0 ms per slice
```

1.0 ms per slice is what the probe measured for the timer itself (0.63 ms for a
sub-millisecond request, 1.67 ms at 1 ms). The probe's 84.0 ms prediction assumed
~3.5 slices; the real frame takes 3.78, and the difference is that extra slice
plus VM noise. **Left alone deliberately** — biasing the request down to cancel
a 1 ms overshoot would be fitting the port to one VM's timer, and 4% on an engine
[designed at 12 Hz](frame-timing.md) is not perceptible. The number to watch if
this is ever revisited is slices/frame, not fps.

## The Windows build — 2026-08-03

**`theoc.exe` builds, links and packages.** Nothing has run on Windows, so this
section is about what the compiler and linker settled, not about the game
working. Produced entirely by cross-compiling from macOS:

```sh
tools/package-windows.sh          # -> dist/theoc-windows-x64-<version>/
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

`dist/theoc-windows-x64-<version>/` — launcher, `bin/theoc.exe`, seven DLLs, the timing
probe, README. The DLL list is **not hand-written**: it is the transitive import
closure of `theoc.exe` walked with `objdump` and filtered against a denylist of
DLLs that ship with Windows, because the Linux bundle already established that
guessing does not work and the loader is the oracle.

Two things measured rather than assumed:

- **119 MB**, of which `avcodec-61.dll` alone is 89.6 MB. (The
  [before/after table](#both-bundles-minus-the-ffmpeg-nobody-uses) below records
  **131 MB** for the same bundle. Both are real `du` readings taken a day apart —
  2026-08-03 here, 2026-08-04 there, with the timing probe added in between — and
  the pre-swap bundle no longer exists to re-measure. Neither figure is load-
  bearing: the point either makes is "about 120 MB of ffmpeg this port cannot
  reach".) The closure is seven
  DLLs against Linux's ~160-library graph, but that is **not** because it carries
  less — this ffmpeg links its codec dependencies *into* the av\* DLLs. The Linux
  note therefore still stands: the port only decodes MPEG-1 cutscenes, and a
  minimally-configured ffmpeg would cut this dramatically. (The first draft of
  the packaging script claimed "~50 MB" from reasoning rather than measurement,
  and was wrong by 2.5x.) **Done 2026-08-04 — the bundle is now 7.3 MB**; see
  [Both bundles, minus the ffmpeg nobody uses](#both-bundles-minus-the-ffmpeg-nobody-uses).
- **Stripping `theoc.exe` takes it from 14.1 MB to 2.4 MB**, and the symbols are
  regenerable by rebuilding.

There is no system-integration hazard to reason about, which on Linux was the
hard part: the Windows equivalents of libX11/libGL (`d2d1`, `DWrite`, `USP10`,
`ntdll`, `msvcrt`) are all system DLLs the denylist excludes anyway, so the rule
falls out instead of needing a judgement call. `libwinpthread-1.dll` **is**
bundled — it comes from the toolchain, not from Windows — while libgcc and
libstdc++ are statically linked and so never appear.

### First run on Windows: the single-instance lock, and a latent bug on every platform

The first execution got **further than expected and then looped forever**. The
boot is line-for-line identical to macOS — 79 COPY relocs, 34,994 relocs, 0 zero
GOT/PLT slots, 10/10 libmvos and 215/215 game constructors clean, all nine
subsystem flags set, plugins loaded, audio open, `cIntuition` constructed — and
then, inside `Start`:

```
  [net] socket(type=1) -> guest fd 4
  [net] bind(:5043) faked OK — single-instance lock
  [abort] ignored (bring-up; THEOC_LOUD_ABORT=1 to trap)
  ... the whole of Init runs again, forever
You can run only one Theocracy in the same time!
```

**Root cause: `listen()` on an unbound socket.** The single-instance lock fake
returned success from `bind` *without binding anything*
([guest-libmvos.md](guest-libmvos.md), "The single-instance lock stays faked").
That worked on macOS and Linux **by accident**: POSIX `listen()` auto-binds an
unbound socket to an ephemeral port, so the game's next call succeeded anyway.
Verified rather than assumed — a five-line probe on macOS returns `listen() == 0`
and `getsockname` then reports a kernel-chosen port. Winsock has no such
behaviour: `listen()` on an unbound socket fails with `WSAEINVAL`, the game reads
that as "the port is taken", and `Fatal`s.

The fix binds the socket to an **ephemeral loopback port** instead of returning
early. The purpose of the fake was only ever to avoid *holding* `:5043` so a
second instance can boot for multiplayer testing — not to leave an unbound
socket behind. All three platforms now take the same path.

**This is a latent bug in the macOS and Linux ports, not a Windows-ism.** They
depended on an implicit `bind` nobody had decided to depend on, and it went
unnoticed for as long as every host happened to provide it. That is the same
shape as the `st_blksize` and `d_type` assumptions this port has hit before, and
the reason a third platform is worth having at all.

**Second finding: an ignored `abort` can spin forever.** The handler's own
comment already warned that ignoring `abort` risks "a silent OpenSubsystems
restart" — and that is precisely what happened, 16 times over, until the run was
killed. Ignoring `abort` returns into guest code that has already concluded it
cannot continue, so everything after is undefined. The ignore is now **bounded**
(`THEOC_ABORT_CAP`, default 32): past the cap the host stops and prints the
diagnosis, instead of leaving someone to infer it from a repeating log. A healthy
run aborts zero times.

### The same Fatal, a second time: `WSAStartup` was never called — 2026-08-04

The bundle rebuilt with the [minimal ffmpeg](#both-bundles-minus-the-ffmpeg-nobody-uses)
failed in `Start` with the identical message — "You can run only one Theocracy in
the same time!", 32 ignored aborts, `Start aborted` — on a build whose *only*
change was which ffmpeg it linked. Everything before `Start` was byte-for-byte
the healthy boot: 79 COPY relocs, 34,994 relocs, 10/10 and 215/215 constructors,
all nine subsystem flags, plugins, audio, `cIntuition`.

**The tell was an absence.** Where macOS logs

```
  [start] THEOC_START_SEC unlimited
  [net] socket(type=1) -> guest fd 4
  [net] bind(:5043) faked OK — single-instance lock
```

Windows logged `[start]` and then went straight to `[abort]`. Not a *different*
`[net]` line — no `[net]` line at all. The socket trap only printed on success,
so a host that could not create a socket showed up as a gap.

**Root cause: nothing in the port ever called `WSAStartup`.** Winsock requires a
per-process initialisation before any socket call; POSIX has no equivalent, so
there was nothing to port and the call was simply never written. Every
`socket()` returned `WSANOTINITIALISED`, the guest's IPC lock could not open, and
the engine reports that as the port being taken.

**Why it worked for a month anyway.** The staged full-fat ffmpeg's DLLs carry
network-capable transports whose own initialisation calls `WSAStartup`, so by the
time the guest asked for a socket, Winsock was already up. Cutting ffmpeg down to
`--disable-network` removed the accident, and with it a netgame that had been
verified by play. **The bundle-size work caused this**; the size result stands,
but the Windows netgame verification of 2026-08-04 was obtained on a binary that
was only working by borrowed initialisation.

**This is the listen()-auto-binds bug again, in a different costume** — an
implicit initialisation nobody chose to depend on, load-bearing right up until a
component declined to provide it. The difference is that this one was hidden
behind a third-party DLL rather than a kernel behaviour, so no amount of reading
the port's own source would have found it.

Fixed by calling `WSAStartup(2.2)` once from the `TrapLayer` constructor, next to
the rest of host setup, so a failure is reported at boot rather than inside a
trap. `WSANOTINITIALISED` is now mapped explicitly (to `ENETDOWN`) instead of
falling through to the generic unknown-error 5, and **`socket()` logs its
failures** — both early returns used to be silent, which is the whole reason this
cost a log diff to find. One `fprintf` would have named it immediately.

> The lesson is not "call `WSAStartup`". It is that **a trap which logs only its
> successes turns a host-side failure into a guest-side non-sequitur.** The
> visible symptom here was a message about running two copies of the game.

**Confirmed fixed by play, 2026-08-04.** The rebuilt bundle boots, and the two
things the borrowed initialisation had invalidated were both re-verified on it:
**multiplayer works**, and **cutscenes play** — which is also the first
end-to-end confirmation of the minimal ffmpeg on Windows, previously verified
only on Linux amd64 and arm64. Nothing in the Windows port is now resting on a
result obtained from the pre-fix binary.

### The game runs on Windows — done 2026-08-04

**Windows is playable, and the port is closed.** Three hosts now run the same
2000 i386 binaries from the same source, with no `#ifdef` in any unit but
`traps.cpp`.

The instrumented 6-minute run above is the structural half. Everything it could
not reach was then **played**: save/load and a full netgame, both reported
working. Save/load was the one that mattered, because it is the only failure in
this port that is silent — `O_BINARY` is applied at every open, and had it been
missed anywhere, CRLF translation would have corrupted a `.tsg` on write and
nothing would have complained until a campaign failed to reload. Multiplayer
matters because **Winsock is the largest rewritten surface in the port** and
until now only its single-instance-lock corner had ever executed.

*Provenance: as with [Linux](#confirmed-by-play), the play confirmation is the
maintainer's report from an interactive session, not an instrumented run. Right
kind of evidence for "does it work", wrong kind for any number — which is why
every figure in this section comes from the log instead.*

**Two risks were ranked and both were wrong, in the useful direction:**

- **Path handling never bit.** `resolve_path`'s `guest[0] == '/'` test is still
  not how Windows spells "absolute", and it was the standing prime suspect for
  the next failure. Every path the game actually asks for is relative to the
  data root, so the branch is simply never taken. Latent, not live.
- **Nothing else needed a Windows-specific fix at all.** The whole distance from
  "boots to `Start`" to "playable" was one latent bug shared by all three
  platforms, plus a sleep primitive. That is the dual-image architecture paying
  off: the guest is the same binary everywhere, so a host port only has to be
  right about the OS boundary — which is finite and enumerable, and was
  enumerated in M0.

The clean-exit counters, for a baseline to compare future runs against:

```
implemented imports hit: 64  (3575613 calls)
UNIMPLEMENTED hit:       0  (0 calls)
guest heap:              13.4 MB live, 32.1 MB frontier, 128 MB arena
Guest-libmvos: Init=ok OpenSub=ok Start=ok
```

Heap was flat at 31.4 MB live and `+0.00 MB/s` frontier across the entire
province sit; fds 2, stubs 144 B, guest ESP constant — the same figures both
other platforms report.

### What is still not known

Nothing here blocks playing the game. In rough order of how much they would
change if someone looked:

1. **Timing under contention, and on bare metal.** Every timing number this
   project has for Windows comes from one idle VM. The probe's `--busy N` mode
   was written precisely for the three-instances-on-one-host case, and the
   [Linux multiplayer paragraph](#confirmed-by-play) already explains why an
   impression of slowness under those conditions would not be usable evidence.
   **What closes this is below** — two probe runs and one bare-metal run, all
   specified, none of them requiring a code change.
2. **Audio is very slightly noisier than macOS.** Province steady state ran
   `underrun=0/s` in 170 of 191 samples, the rest 7–766 frames/s — at most ~17 ms
   of audio in a second, and below anything audible. macOS reports a flat zero.
   Unexplained rather than understood; the large spikes (94k, 26k, 20k) are all
   at scene loads and are the [already-documented](frame-timing.md) load stall,
   not this.
3. ~~**Path handling**~~ — **fixed 2026-08-04**, see below. Still worth knowing
   that no run has ever exercised the branch.
4. ~~**`THEOC_WATCHDOG_SAMPLE`**~~ — **fixed 2026-08-04**: it now says it is
   macOS-only instead of appearing to run. Still macOS-only.
5. ~~**`CloseSubsystems` is still skipped**~~ — **closed 2026-08-04, as a
   decision rather than a fix.** Windows was the platform
   [host-architecture.md](host-architecture.md) named as the revisit trigger, on
   the theory that a device left open by the guest's bookkeeping would matter
   there. It played, and nothing surfaced on any of the three hosts. The revisit
   trigger is now the host stopping being one-shot, not a fourth platform.

### What closes the timing item — 2026-08-04

Three runs and no code change. Two of them can be done today; the third waits on
hardware. The in-game half is already done and shipped.

**The in-game half — done.** `[fps]` now prints **`(N slices/frame, +M ms
each)`** directly, measured at the sleep call. The 2026-08-04 residual above was
decomposed by hand out of three separate columns to reach "≈1.0 ms per slice";
that arithmetic is now the instrument's job, on every host, so the next person to
question timing on unfamiliar hardware reads it instead of deriving it. See
[diagnostics.md](diagnostics.md), "Reading the sleep slices".

**Two probe runs in the VM.** The probe already ships inside the bundle:

```
win-timing-probe.exe --busy 12
win-timing-probe.exe --busy 24
```

**Not `--busy 4`.** The VM has 12 logical CPUs, and four spinning threads on
twelve never force the scheduler to choose — it would measure the idle case
again while looking like a contention test. The point is to oversubscribe: 12
matches the CPU count and 24 doubles it. Compare against the idle table above;
the columns that matter are test 2's median lateness and test 3's
frame/ticks-per-frame.

**One bare-metal run**, when hardware exists — see the note below. Same three
invocations (idle, `--busy 12`, `--busy 24`), plus one ordinary game session with
`THEOC_FPS=1` to read the two new columns in situ.

**What would count as a problem**, decided in advance so the result is not read
after the fact:

- **slices/frame falling toward ~1** — the heartbeat has collapsed into the frame
  rate, which is a defect and not a slow host.
- **slices/frame × overshoot exceeding ~8 ms** — that is ~10% of an 83 ms frame,
  the point at which this stops being invisible and becomes a game-speed error,
  since the limiter is elapsed-based.
- **median tick lateness above ~16 ms** — half a heartbeat interval, past which a
  tick is closer to the next one than to its own.

Anything short of those is the timer's floor under load, which is a number to
record rather than a thing to fix.

> **Bare metal is scheduled, not abandoned.** No bare-metal Windows machine is
> available to this project; one becomes available to the author **from roughly
> 2026-08-18** via a third party. Until then every Windows figure in this
> document describes one VM and should be read that way. This is the whole
> remaining content of the item — the VM half is answerable now.

### The contention runs — 2026-08-04

Both ran in the VM, 12 logical CPUs, waitable-timer rows quoted (that is the
path the port takes). The idle column is the table earlier in this section.

| | idle | `--busy 12` | `--busy 24` |
| --- | --- | --- | --- |
| single 0.100 ms sleep, **min** | — | 1.37 ms | **0.84 ms** |
| single 0.100 ms sleep, median | ~0.6 ms | 6.64 ms | 26.66 ms |
| 30 Hz tick, median lateness | ~0.1 ms | **6.24 ms** | **22.96 ms** |
| province frame, median | 87.0 ms | 97.9 ms | 98.8 ms |
| ticks/frame | ~3.5 | 2.87 | 2.93 |

**Against the criteria, which is the point of having written them down first:**

- **slices/frame falling toward ~1** — did not happen. 2.87 and 2.93, barely
  moved from idle. The heartbeat does not collapse into the frame rate under any
  load tested. **Passes.**
- **median tick lateness above ~16 ms** — 6.24 ms at `--busy 12`, **22.96 ms at
  `--busy 24`**. Passes at saturation, fails at 2× oversubscription.
- **overshoot exceeding ~8 ms of the frame** — **fails both**, at +14.6 ms and
  +15.5 ms. But this criterion was mis-specified, and saying so is not the same
  as excusing the result; see below.

**The mis-specified criterion.** It was written as "slices/frame × overshoot",
which assumes the per-slice overshoots *add up* across a frame. They do not: the
slice loop charges **real elapsed** time against the remaining budget and
recomputes the next slice, so one slice that overshoots by 20 ms eats the
remainder of the frame instead of extending it. The measurement shows this
directly — between `--busy 12` and `--busy 24` the single-shot median degrades
**4×** (6.6 → 26.7 ms) while the frame moves **0.9 ms** (97.9 → 98.8). The frame
floor is the 83.3 ms budget plus roughly *one* overshoot, not N of them. The loop
is self-limiting, which is a property worth knowing and was not designed in.

**What the runs actually found is not a timer problem.** Under load all four
sleep methods converge — at `--busy 24`: `Sleep()` 101.9 ms, `Sleep()+tbp`
98.9 ms, timer 98.8 ms, timer+tbp 99.1 ms. The waitable timer is never *worse*,
so the port's choice stands, but its advantage shrinks from decisive to ~3 ms
because the binding constraint has changed. The min column proves which: at
`--busy 24` the timer still fires a 0.1 ms sleep in **0.84 ms**, where
`Sleep()+timeBeginPeriod(1)`'s best case is 10.99 ms. The timer's resolution is
intact; what costs 26 ms is waiting for a CPU to run on afterwards. No sleep
primitive fixes that, and nothing in `traps.cpp` should try.

So the honest statement of the residual: **with no idle core available the
province frame stretches from 83 ms to ~98 ms — 12 fps down to ~10.2, about 15%
slow.** That is a real game-speed error, because the limiter is elapsed-based,
and it is a limit of the host rather than a defect in the port.

**Two corrections to what this section said before the runs.** First, it claimed
the three-instances-on-one-host multiplayer case "sits between" `--busy 12` and
`--busy 24`. It does not, and the claim oversold the test: a Theocracy instance
spends most of its frame asleep, so three of them are nowhere near twelve
CPU-saturating spinners. `--busy 12` is already well past the case that raised
this. Second, the VM reported `NtQueryTimerResolution current: 1.0000 ms` before
the probe started anything — something else on that machine had already raised
the global timer resolution, so neither run measured the 15.625 ms default. The
port does not call `timeBeginPeriod`, and test 1's min column says it does not
need to, but "a machine where nothing has raised the global resolution" remains
untested and is now part of what bare metal answers.

**Status: the sleep-primitive question is closed.** The waitable timer is the
right call, it holds up to 2× oversubscription, and no code change is indicated.
What is left is one bare-metal run, for the reasons above and in the note.

### Two latent defects closed — 2026-08-04

Neither changes what any run does today. Both were found by porting rather than
by failing, which is the argument for having done the port at all.

**`resolve_path` tested `guest[0] == '/'` for "is this absolute", and that has a
Windows-shaped hole in *both* directions.** A host path such as `D:\theocracy`
did not read as absolute; and a guest path such as `/home/x/y` read as absolute
and was handed straight to a Windows API, which resolves a leading separator
against the **current drive** — a real location, silently the wrong one, and it
would have surfaced as a missing file rather than as anything recognisably a
path bug. Now a `path_is_absolute()` helper knows both spellings (`/`, plus
`C:\`, `C:/`, `\\host\share` and a bare leading `\` on Windows).

The Unix-absolute case still **passes through unchanged**, because there is no
honest mapping for it: `/dev` and `/mnt/cdrom` are already handled above it, and
anything else names a Linux filesystem the host does not have. It now warns
once, per distinct path. Inventing a translation would be guessing, and the
branch has never fired in a run — the log of the 6-minute Windows session shows
the guest asking only for `/dev/dsp` and relative paths (`save/save4.tsg`,
`movie/intro.mpg`).

**`THEOC_WATCHDOG_SAMPLE` shelled out to `sample` on every platform.** It is an
Apple developer tool, and the command's redirect syntax is a POSIX shell's
rather than `cmd.exe`'s, so off macOS `std::system` returned a shell error whose
code was printed as a bare `rc=` — which reads as *the sampler ran and failed*
rather than as *the feature does not exist here*. It now says which. That
mattered more than its size suggests: the line only ever prints during a stall,
i.e. exactly when the reader can least afford a misleading one.

Linux could plausibly use `gdb -p … -batch -ex "thread apply all bt"`. Not
shipped: putting unverified code in the one path that runs only when something
is already wrong is a bad trade, and verifying it needs a Linux stall to point
it at.

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
  the [heap-growth trials](heap-growth-trials.md): the +18 KB/cycle is the *guest*
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
`dist/theoc-linux-<arch>-<version>/` — a launcher, `bin/theoc`, `lib/`, and a README.
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
  bundle is ever distributed rather than just tested. **Done 2026-08-04 — 37 MB**;
  see [Both bundles, minus the ffmpeg nobody uses](#both-bundles-minus-the-ffmpeg-nobody-uses).
- **glibc ≥ 2.36 and libstdc++ from GCC 12** on the target, inherited from the
  bookworm base. For an older target, build the image `FROM` an older base
  rather than bundling around it.

## Both bundles, minus the ffmpeg nobody uses

**Done 2026-08-04.** Both bundles were ~90% ffmpeg that this port cannot reach.
`tools/build-ffmpeg-min.sh` builds a minimal one; the package scripts pass it to
cmake as `-DTHEOC_FFMPEG_PREFIX=` and are otherwise unchanged.

| Bundle | Before | After |
|---|---|---|
| Windows x64 | 131 MB | **7.3 MB** |
| Linux amd64 | 189 MB | **37 MB** |

`avcodec` carries it: 89.6 MB → 613 KB on Windows, and on Linux the entire codec
dependency graph (x264, x265, vpx, theora, srt, zmq, and their own dependencies)
stops being reachable from the binary at all, so the closure walk never bundles
it. What is left on Linux is SDL2, Unicorn and the X11/Wayland/ALSA/Pulse client
libraries, which is the floor for that bundling policy.

### What the port actually decodes, measured

All 27 files under `data/cd/movie/` probe identically — **MPEG-PS containing
mpeg1video and mp2 44.1 kHz stereo** — and `port/src/mpeg.cpp` additionally uses
swscale (YUV→RGB565) and swresample (audio format). The engine's own FLC video
never touches ffmpeg; that is guest code decoding into the LFB.

### The trap: "what the files contain" is not the enable list

Configuring for exactly that — `--enable-decoder=mpeg1video,mp2
--enable-demuxer=mpegps` — builds cleanly, shrinks the bundle, and **decodes
nothing**:

```
[mpeg] probed stream 0 failed
[mpeg] Could not find codec parameters for stream 0 (Video: none, none)
[smpeg] decode failed, will skip frames
```

An MPEG-PS elementary stream whose type the container does not state is
identified by libavformat *probing* it, and the probe works by asking the **raw**
demuxers — `mpegvideo`, `mp3` — to recognise their own bitstream. With those
disabled the codec id stays `NONE` and no decoder is ever looked up, however
enabled the decoder is. The probe also reports MPEG-2 for MPEG-1 video, so
`mpeg2video` is enabled alongside `mpeg1video`; it decodes both and shares their
code, so it is free.

**The reason this is verified rather than reasoned about** is in the third line
above: the port logs the failure and *carries on to the menu*. A bundle 95%
smaller with every cutscene silently missing presents as a clean success.

### How it was verified, with no display

Headless in the container, both architectures, reading the decode log rather than
the screen:

```
[mpeg] decoded 'data/cd/movie/ubi_logo.mpg' 480x360 249 frames @ 24.0 fps, audio 198144 samp (9.0s)
[mpeg] decoded 'data/cd/movie/logo.mpg'     608x300 476 frames  @ 24.0 fps, audio 438336 samp (19.9s)
[mpeg] decoded 'data/cd/movie/intro.mpg'    608x300 1192 frames @ 24.0 fps, audio 1096128 samp (49.7s)
```

Frame and sample counts are identical on amd64 and arm64, and identical to what
the full-fat ffmpeg produced. **Windows is verified as far as cross-building
allows** — same configure line, same sonames, closure resolved, bundle built —
but nothing has *played* a cutscene there since the change; that is a task in
[`todo.md`](../../todo.md).

Two smaller things this dragged out:

- **`nasm` had to join the Dockerfile.** ffmpeg's configure refuses to build on
  x86-64 without an assembler, so an amd64 bundle build failed in the container
  before it reached the port. arm64 never hit it — there is no x86 asm to
  assemble — which is a good reminder that the emulated-arch build is not a
  rehearsal for the real one.
- **Both package scripts now wipe their build directory first.** `find_library`
  caches, so a tree left over from a run with a different ffmpeg prefix keeps
  linking the old one and ships it silently. One extra minute of compiling
  against a failure that looks like the prefix not working at all.

## Re-cutting the bundles found the same bug a second time — 2026-08-19

`v1.0.0` was re-pointed at the tip and all three bundles rebuilt. **Linux amd64
did not compile**: `cdaudio.cpp` uses `std::vector` and never included
`<vector>`. One line to fix, and the same defect as `std::floor` in `video.cpp`
above — code that only ever met libc++, which supplies the header transitively
where libstdc++ does not.

What is worth recording is not the bug but *why it was sitting there*. The
bundles were cut 2026-08-04; `cdaudio.cpp` first landed 2026-08-08 in `f5b899c`,
with the Redbook work. Between those dates `port/src` gained about 1800 lines —
`cdaudio.cpp` (386), `config.cpp` (194, an entirely new translation unit) and
`traps.cpp` (+866) — **none of which any toolchain but AppleClang had ever
seen.** The port compiles on three hosts, but only one of them was being asked.

So the interval between a release and the next bundle re-cut is a window in
which a single compiler is the only checker, and it is the most permissive of
the three. The cheap correction is to run `tools/package-linux.sh amd64` when
`port/src` changes rather than only when something is being released; it is one
container build, and it is the only one of the three that reads the code with a
different standard library. Cross-compiling has now caught a real macOS defect
twice, which makes it a habit rather than a coincidence.

**And a second one from the same re-cut, this time Windows-only.** mingw warned
four times at one line — a narrowing `uint32_t`→`u_char` plus three uninitialised
members — on `inet_ntoa(in_addr{ip})` in the `gethostbyname` trap. POSIX's
`in_addr` is a bare `{ in_addr_t s_addr; }`, so brace-init is right there and
wrong on Windows, where it is a **union whose first member is four `u_char`s**:
the initialiser lands in `s_b1` and the other three octets are zero. The log line
printed `192.0.0.0` where Linux printed `192.168.1.10`.

The bug is confined to the diagnostic — the address the guest actually receives
comes from the `m.w32` two lines above and was never affected — but it is a
netgame log line on the platform whose bare-metal session is still outstanding,
and the point of stamping and logging is that a tester's report can be read at
face value. `s_addr` is the one member name both platforms spell identically.

Both defects came from the *same* re-cut, which is the argument for the habit
above stated more cheaply: two compilers, one afternoon, two real findings, and
neither was visible to the toolchain the port is developed on.

### The arm64 bundle shipped with no build stamp

Checked after the re-cut, per binary rather than per build log — and the arm64
bundle carried `000000`/`00000000`, the CMakeLists' fallback, where macOS, amd64
and Windows all carried the real `260819`/`1ee0ebb0`.

`tools/package-linux.sh` resolves `THEOC_VERSION` **on the host** and passes it
in, and its comment says why: the bind-mounted `.git` is owned by another uid, so
git inside the container fails `detected dubious ownership`. That is confirmed —
`rev-parse`, `log` and `status` all fail that way in both images. But the script
passed only the *version*, leaving `THEOC_STAMP_DATE` and `THEOC_COMMIT` to the
`execute_process` calls in `port/CMakeLists.txt`, which run **inside** the
container and hit exactly the failure the comment describes.

Why amd64 got real values anyway is unexplained. Re-running that same configure
in that same image afterwards produced a *half* stamp — `000000` plus the correct
commit — and arm64 produced all zeros: three outcomes from one commit. The point
is not to find out which condition tips it, but that the stamp exists to make two
builds of one source produce identical bytes, and a value that varies by
architecture and by run cannot do that job. So the fix is the one the script had
already made for the version: resolve on the host, pass `-DTHEOC_STAMP_DATE` and
`-DTHEOC_COMMIT` alongside `-DTHEOC_VERSION`. The dirty flag and the 7-character
clamp are duplicated from the CMakeLists and noted there as needing to move
together.

Windows was never affected — it cross-builds on the host, where git works. Its
stamp is simply harder to *see*: mingw embeds the two short literals as
instruction immediates, so `strings` finds `1ee0ebb0` inside a function prologue
and the date only as the fragments `2608` and `19`. Confirm a PE's stamp by
disassembling around the literal, not by grepping for it — a grep that finds
nothing there is not evidence of a missing stamp.

**The general lesson, and the third of the day.** All three of this session's
findings were invisible in a green build log: the bundle built, the exit code was
0, and the defect was in what the artefact *contained*. `docs/porting/diagnostics.md`
says the first log line names the build; that is only true if something checks
that the name is there. Verify the artefact, not the build.

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

~~**Introduce `port/src/platform/` rather than sprinkling `#ifdef`s**~~ — at
Winsock, which is where it finally earns its keep. The macOS-isms are
concentrated — sockets, sleep, clock, filesystem, one shell-out — so a thin
interface with three implementations keeps `traps.cpp` readable. Scattering
conditionals through a 4000-line file is how this stops being maintainable. Two
sites did not earn an interface, which is why Linux correctly did not build one;
Windows touches every item on that list.

> **This prediction was wrong, and the port is finished without it.** Windows did
> *not* touch every item on that list. Clock and filesystem needed nothing beyond
> a one-line `theoc_mkdir` and `O_BINARY` at the open sites; the shell-out is
> `THEOC_WATCHDOG_SAMPLE`, still unported and off by default. What Windows
> actually needed was **two** contiguous blocks in `traps.cpp` — Winsock, and
> `theoc_sleep_us` — sitting next to each other, plus scattered `O_BINARY` flags
> that an interface would not have collected anyway.
>
> The reasoning ("scattering conditionals is how this stops being maintainable")
> is still right; the premise it rested on — that the macOS-isms were spread
> across five subsystems — was an inventory made before anyone had ported
> anything. Three platforms now build from two adjacent `#if` blocks. Should a
> fourth host or a genuinely different subsystem arrive, revisit; until then a
> directory holding two functions is worse than the two functions.

**Teardown was on this list and is now off it.** `CloseSubsystems` is skipped
because process exit reclaims everything it would release, and "a platform where
that is less true" was the stated revisit trigger. Windows was that platform, it
played, and nothing surfaced — so the skip is a closed decision, not a deferred
item. See [host-architecture.md](host-architecture.md), "Why teardown skips
`CloseSubsystems`", for what would reopen it (the host ceasing to be one-shot —
not a fourth host).

## CI: building the bundles on GitHub — 2026-08-20

The bundles were cut by hand until now, which is how the 2026-08-19 re-cut
shipped three defects out of three green builds. `.github/workflows/release.yml`
moves the cutting onto GitHub Actions. Four decisions in it are load-bearing.

**One workflow, not two.** The obvious split — build on tag, publish on a
GitHub Release event — does not survive contact with this repo's topology.
`origin` is Gitea with Actions disabled; it push-mirrors to GitHub, tags
included. A mirror pushes *git objects*, and a Release is an API object, so
`on: release` could only ever be fired by hand in the web UI. Worse, the split
puts the build in one workflow run and the publish in another, and artefacts do
not cross runs without a token, an explicit `run-id`, and a race against the
retention window. Hanging the release job off the build jobs with `needs:` keeps
the artefacts inside one run, and **`--draft`** supplies the human gate the
two-trigger design was reaching for: the tag builds and uploads, a person
decides whether it becomes a release.

**Tag-only, with a dispatch escape hatch.** Nothing runs on an ordinary commit
push — deliberate, and cheap given the 2000-minute budget. The cost is real and
worth stating: a broken build now surfaces when you cut a release rather than
when you break it. `workflow_dispatch` is the mitigation (it builds everything
and skips the release job), and force-pushing a tag re-triggers the run, which
is already how this repo fixes a bad tag. One trap: **the workflow file is read
from the ref that was pushed**, so a tag only builds if it points at a commit
that already contains it.

**The workflow is a wrapper, not a build system.** It chooses the container
runtime (`CONTAINER=docker`, which `package-linux.sh` and `build-ffmpeg-min.sh`
already parameterise for exactly this) and otherwise calls the scripts. This is
not tidiness: everything expressed in YAML can only be tested by pushing to
GitHub, and everything expressed in shell can be tested on the machine in front
of you. Keeping the YAML thin shrinks the surface that only CI can prove to
runner labels, cache keys and secrets.

**The smoke test asserts on bytes, not on an exit code.** `tools/smoke-test.sh`
synthesises a `.tsg`-shaped fixture, runs the binary under `THEOC_FIX_SAVE`, and
compares the stamp the binary wrote into the header against what `git` says.
`THEOC_FIX_SAVE` is the only path that needs neither a display nor the game
tree, which is what makes it runnable in CI at all — and it calls
`set_build_identity()` before it returns, so the repaired file carries the
identity.

Two details that decide whether the test is worth anything:

- **`collapse_save_file()` returns `void` and swallows every error** — an
  unreadable file, a short file, a header it does not recognise. So the process
  exits 0 whatever happens, and asserting on the exit code would assert nothing.
  The check has to read the file back.
- **The fixture has to clear the header guard**: at least `0x48` bytes, the
  `theosg42` magic at `0x40`, and a NUL-terminated name leaving at least the
  22 bytes the stamp needs. The body is zeroed so the run scanner rejects it as
  flat fill and leaves the structure alone — only the header path is under test.
  Nothing copyrighted is involved; the layout is documented in
  `tools/fix_save.py`.

The test was checked in both directions before being trusted, which matters more
than it sounds: a smoke test that cannot fail is worse than none. Against a build
tree configured before the current commit it reports the stale stamp and exits 1
(`binary says 8f4b26081906e44ee+ma00 / git says 8f4b260820b3bd06e0`); against a
freshly configured build it passes. That stale-configure case is not contrived —
build identity resolves at *configure* time, so a build directory that survives a
commit produces exactly this, and it is why both packaging scripts `rm -rf` theirs.

The job also fails if the bundle exceeds 80 MB. `package-linux.sh` degrades
gracefully when the minimal ffmpeg is missing, using the distro one instead, so a
cache miss that also failed to build would otherwise ship a ~190 MB bundle
quietly rather than failing.

**The first container run of it found a defect in the commit before.** Adding
the SPDX headers rewrote each file through a temp file and `mv`, which handed
four scripts the temp file's `644` — `package-linux.sh` and `build-ffmpeg-min.sh`
among them, both invoked directly by the workflow. Nothing on macOS noticed,
because nothing had run them since; the failure would have been the first CI
run's packaging step. Worth recording as a pattern rather than an incident: a
mechanical pass over many files preserves content and quietly drops mode, and
`git ls-tree -r <ref> --format='%(objectmode) %(path)'` is how you see it,
because `git diff` shows the mode change only in `--summary`.

Verified end to end on 2026-08-20: `package-linux.sh arm64` at a clean HEAD
produced a 37 MB bundle, and `smoke-test.sh` run against it *inside* the
`debian:bookworm-slim` build image — the closest local stand-in for an
`ubuntu-24.04-arm` runner — reported `8f4b26082048239070la00`, matching the
commit. The image already carries `git` and `python3`, which is what the test
needs beyond the bundle.

**The first real run found the one thing macOS structurally cannot show.** The
packaging container runs as root against a bind-mounted repo, so `dist/` comes
back owned by root; the runner user then cannot write the tarball into it. On
macOS this never happens, because Rancher/lima translates uids across the mount
— the local container run reproduced the *architecture* faithfully and the
*ownership semantics* not at all. `sudo chown -R` on the output directory after
packaging is the fix. Worth generalising: a containerised build verified on
macOS has not been verified for file ownership anywhere.

### Windows joins CI — 2026-08-20

`tools/stage-win-deps.sh` is the missing half of `package-windows.sh`: it
downloads the SDL2 mingw development tarball (2.32.10), cross-builds Unicorn
(2.1.3) at `-DUNICORN_ARCH=x86 -DBUILD_SHARED_LIBS=OFF`, and delegates ffmpeg to
`build-ffmpeg-min.sh`. Verified by staging into a scratch prefix and packaging
against it — 7.4 MB, the same seven DLLs as the hand-staged tree.

**It deliberately does not stage ffmpeg into `deps-win/`, and that is a licence
fix.** The hand-staged tree carried `ffmpeg-n7.1-latest-win64-gpl-shared`, a
prebuilt binary configured with `--enable-gpl`. Nothing GPL ever shipped —
`package-windows.sh` prefers `deps-ffmpeg-win/`, and the bundle's `avcodec-61.dll`
measures 612 KB against the GPL build's 94 MB — but the preference is a
*fallback*, not a guard: if the minimal build ever failed, the packaging script
would quietly link the GPL one instead, and the port ships GPL-2.0-**or-later**
precisely because Unicorn 2.x forbids anything later. Staging no ffmpeg into
`deps-win` turns that silent substitution into a build failure, which is the
behaviour worth having. The hand-staged copy on the development machine is
untouched and still holds it.

**The `.exe` can be smoke-tested after all, and does not have to ship
unverified.** The open question was that a cross-built Windows binary cannot run
on a Linux runner. Wine answers it: `THEOC_SMOKE_RUNNER=wine` prefixes the
invocation, and `THEOC_FIX_SAVE` needs no display, so the same test that checks
the Linux bundles checks this one. Confirmed in a container against a
cross-built bundle — `8f4b26082009339110w600`, the `w6` being the host+arch pair
the stamp format documents.

A static fallback was measured and is worth knowing about, because it is weaker
than it looks: `THEOC_COMMIT` survives in the stripped `.exe` as a contiguous
literal and can be grepped, but `THEOC_STAMP_DATE` does **not** — the compiler
inlines the six characters as immediates, so `strings` never sees `260820`. A
static check could therefore assert the commit and not the date. Wine asserts
both, which is why it is what the job runs.

**The first CI run of the Windows job failed on one DLL**, and the cause is a
packaging assumption that had never been portable. `package-windows.sh` resolves
the import closure against a fixed list of directories, and that list carried
Homebrew's layout — where the toolchain runtime DLLs sit in the toolchain's
`bin/` — plus `/usr/x86_64-w64-mingw32/bin` for Linux, which is the wrong half of
the Debian layout. Debian and Ubuntu ship `libwinpthread-1.dll` in
`/usr/x86_64-w64-mingw32/**lib**/` (package `mingw-w64-x86-64-dev`), so every
other DLL resolved and that one did not. The script was right to fail rather
than ship: a bundle missing it does not start on a clean Windows machine.

Adding the `lib/` path fixes it, with one ordering constraint worth stating —
the i686 tree carries a same-named DLL of the wrong architecture, so no path
that could reach it belongs in the list. Verified by reproducing the runner
rather than reasoning about it: staging and packaging inside `ubuntu:24.04`
bundles all seven DLLs, `file` reports every one as PE32+ x86-64, and the
resulting bundle passes the wine smoke test at `8f4b260820d6a5c59+w600`.

One local-only trap met on the way, recorded so it is not re-met: **wine in an
arm64 container cannot run an x86-64 `.exe`** and hangs rather than failing. The
verification runs must pass `--platform linux/amd64`, which is what the runner
is anyway.

**Then wine refused to start at all on the runner**, for the second instance of
the same root cause as the root-owned `dist/`: `WINEPREFIX=/tmp/wineprefix` is
fine in a container, where everything runs as root, and refused on a real runner
— *"'/tmp' is not owned by you"*. `${{ runner.temp }}` is owned by the runner
user and is the right home for it.

That is twice now that a containerised local verification passed on ownership
semantics that do not hold on the runner, and it is worth stating as a rule
rather than as two incidents: **running as root locally verifies behaviour and
not permissions.** Everything a container proves about architecture, linking and
output stays true; everything it appears to prove about who may write where does
not.

### macOS joins CI, signed and notarised — 2026-08-21

macOS had never had a packaging script at all — it was the platform the port was
*developed* on, so it had only ever been a dev build against Homebrew's
`/opt/homebrew` prefix. `tools/package-macos.sh` is the third packaging script
and the last one; the workflow now builds all three platforms and drafts the
release from them.

The shape is the Linux bundle's: a plain directory with `bin/`, `lib/`, a
launcher and a README, shipped as `.tar.gz`. **Not an `.app`**, deliberately —
the game needs a data tree the user supplies from their own CD sitting beside
the launcher, and a folder that obviously contains its data beats an app bundle
that has to be told where the data went. That decision has one consequence,
described at the end.

#### The cut is cleaner than Linux's, and one thing is invisible to it

The Linux bundle needs a hand-tuned denylist because bundling libX11 or libGL
would be actively harmful. macOS has no equivalent hazard: the display, audio
and GPU are reached through frameworks under `/System/Library`, and libc++ and
libSystem live in `/usr/lib`. So the rule is the whole rule — *bundle everything
outside those two prefixes* — and it falls out of the platform rather than
having to be tuned.

What does not fall out is SDL. **Homebrew's `sdl2` is an alias for
`sdl2-compat`, a shim over SDL3, and it does not link SDL3 — it dlopens it**,
first candidate `@loader_path/libSDL3.dylib`. So `libSDL3` appears nowhere in
the `otool` closure, and a bundle built purely from that closure links, loads,
launches, and dies at `SDL_Init` with *"Failed loading SDL3 library"*. This is
the Linux bundle's libasound lesson arriving from the opposite direction: there,
guessing that SDL dlopened ALSA was wrong because Debian makes it a hard
`DT_NEEDED`; here, trusting the load commands is wrong because Homebrew makes it
a dlopen. **The closure is necessary and not sufficient, on both platforms, for
opposite reasons.**

So `libSDL3.dylib` is added by name — and then the load is actually exercised.
The script dlopens the bundled `libSDL2` and calls `SDL_Init(0)`, which
initialises no subsystem and needs no display but *is* what makes sdl2-compat go
looking for SDL3. It fails in the packaging script rather than on a player's
machine. The smoke test cannot cover this: `THEOC_FIX_SAVE` returns from `main`
before SDL is touched.

The minimal ffmpeg matters more here than anywhere else. Homebrew's ffmpeg drags
x265, x264, svt-av1, libvpx, dav1d and openssl into the closure: **66 MB against
21 MB**, none of it reachable by a decoder that only ever sees MPEG-1/MP2. So
`build-ffmpeg-min.sh` grew a `macos` target — a native build, no container and
no cross-prefix, the only branch of the three that needs neither.

#### `install_name_tool` invalidates signatures, and arm64 does not forgive it

Rewriting install names to `@rpath` is the ordinary relocatability step. On
Apple Silicon it is also a signature-breaking step: **every dylib and every
linker output already carries an ad-hoc signature, and arm64 macOS refuses to
map a Mach-O whose signature does not match its contents.** It does not refuse
politely — the process is `SIGKILL`ed with no diagnostic.

The first working bundle therefore produced a dylib tree that nothing could
load, and the symptom was `python3` dying with `Killed: 9` in the SDL check
above. Re-signing is not the optional distribution step; it is what makes the
bundle loadable at all. `package-macos.sh` always signs — ad-hoc when no
identity is given, Developer ID when one is — and nothing may be reordered
between the `install_name_tool` pass and the signing pass.

#### The hardened runtime versus Unicorn's JIT

Notarisation requires the hardened runtime, and the hardened runtime breaks the
port twice over. Both were measured, with a test program driving `uc_emu_start`
against the bundle's own `libunicorn`:

| signing | result |
|---|---|
| ad-hoc, no hardened runtime | JIT OK |
| hardened, `allow-jit` + `allow-unsigned-executable-memory` | JIT OK |
| hardened, those entitlements removed | `Could not allocate dynamic translator buffer` |

That is the first break, and `port/theoc.entitlements` is the fix. It grants
exactly those two and nothing else.

The first CI run with real credentials then failed on the entitlements file
itself, for a reason worth recording because the obvious check does not catch
it: **an XML comment may not contain two consecutive hyphens**, the header
comment described signing `--options runtime`, and `codesign` rejected the whole
file with `AMFIUnserializeXML: syntax error near line 13`. `plutil -lint` calls
that file valid; `xmllint --noout` does not. The fix is not to remember the
rule — it is that `package-macos.sh` now passes `--entitlements` on the ad-hoc
path too, so `codesign` parses the file on every local build. That line was the
one part of the signing path a local run had never exercised, which is exactly
why it reached CI.

The second break is subtler. **The hardened runtime turns on library
validation, and a hardened process refuses any dylib whose Team ID differs from
its own** — including ad-hoc signed ones, which have no Team ID at all. A
hardened binary sitting next to Homebrew's dylibs dies in dyld before `main`:

    Reason: ... not valid for use in process: mapping process and mapped file
    (non-platform) have different Team IDs

The tempting fix is `com.apple.security.cs.disable-library-validation`. The
entitlements file deliberately does **not** carry it: the script re-signs every
bundled dylib with the same identity as the executable, so validation is
satisfied honestly rather than switched off. The opt-out was used once, to
separate the two failures from each other while testing, and then discarded.

#### Notarised, not stapled

`notarytool` accepts `.zip`, `.pkg` and `.dmg` and never a `.tar.gz`, so the job
zips the bundle with `ditto` purely as transport. That costs nothing: the ticket
Apple issues is keyed on each binary's cdhash, so the same files then ship in
the `.tar.gz` and Gatekeeper recognises them.

What it does cost is stapling. `stapler` writes tickets into `.app`, `.dmg` and
`.pkg` only — there is nowhere in a plain directory to put one — so **first
launch checks with Apple online**. That is the consequence of not shipping an
`.app`, it is accepted, and the README says so and gives the offline escape
(`xattr -dr com.apple.quarantine .`).

Four traps in the notarisation step, all of which fail quietly rather than
loudly:

- **A trailing newline in a secret is invisible and fatal.** This is what the
  first authenticated run actually died of. `base64 -i key.p8 | pbcopy`, and a
  copy taken off the App Store Connect page, both carry a trailing newline;
  GitHub stores the secret verbatim; and `--key-id ABCDE12345\n` is refused
  with `401 Unauthenticated` — the same answer an unauthorised key gets. The
  credentials were never wrong, and `notarytool history` from a laptop proved
  it by returning "No submission history" with the very same three values. The
  job now strips whitespace from the key ID and the issuer, which cannot
  legitimately contain any, and leaves the certificate password alone, which
  can.
- **`base64 --decode` on macOS accepts a raw PEM body and emits binary garbage
  at exit 0.** So a `MACOS_NOTARY_KEY_P8_BASE64` secret holding the unencoded
  `.p8` produces a key file that is wrong in a way nothing notices until Apple
  answers `401 Unauthenticated`, which names none of it. The job now accepts
  either form and checks that what it wrote is a PEM private key before using
  it. It also authenticates with `notarytool history` — which uploads nothing —
  before pushing 21 MB, so a credential fault fails in seconds rather than after
  the transfer, and warns if the key ID is not 10 characters or the issuer is
  not a UUID. Those two shape checks exist because a 401 does not distinguish
  "this key is not authorised" from "the wrong string is in the wrong secret",
  and the second is the far more common mistake.

- `notarytool submit --wait` **exits 0 on a rejected submission** as readily as
  on an accepted one. The `status` field is the only thing that says. Without
  checking it, a tag would publish a bundle Apple refused.
- `--issuer` is required for a team key and meaningless for an individual one,
  where the JWT carries `sub:"user"` instead of `iss`. Passing it empty is an
  error, so the job appends it only when the secret is set — and does so with an
  `if`, not `[ -n ... ] && ...`, because under `set -e` a bare failing test is
  the exit status of the whole statement and would end the step in exactly the
  individual-key case it exists to support.

The signing certificate goes into a throwaway keychain in `RUNNER_TEMP`, never
the login keychain, and is deleted in an `always()` step. One line in that dance
is the one everyone omits: without `security set-key-partition-list`, `codesign`
finds the key and macOS raises a UI authorisation prompt that no one is there to
answer, so the job **hangs** rather than failing.

Finally, the job refuses to build an unsigned bundle on a tag while still
allowing one on `workflow_dispatch`. A tag is a release and a release must be
signed; a dispatch is a pipeline test and being able to run one without
credentials is worth keeping.

#### What CI still cannot prove about the macOS bundle

The smoke test runs `THEOC_FIX_SAVE`, which returns from `main` before Unicorn
is opened and before SDL is touched. So a green macOS job proves the bundle is
built, relocatable, signed, notarised and *loadable* — and says nothing about
whether the guest actually runs under the hardened runtime. The entitlements
table above is the evidence that it does, and it was gathered by hand. **A
signed bundle still wants one real play session before a release is published**;
it is in `todo.md`.
