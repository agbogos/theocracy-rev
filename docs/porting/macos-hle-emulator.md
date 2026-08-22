# macOS native port — HLE emulator plan

> **⚡ SUPERSEDED (2026-07-22) — pivoted to guest-libmvos.** This documents the
> *pure-HLE native-replace* approach: run only the game under Unicorn and
> reimplement the entire libmvos API natively. It worked up the boot/Init path
> but hit the wall that rendering the menu required hand-reimplementing the whole
> libmvos GUI toolkit (every widget ctor / `Init` / `Paint` / `cGD` primitive /
> font / bitmap — an unbounded surface). The project now maps both
> `theocracy.real` and the real `libmvos.so` under Unicorn and HLE-only the
> finite OS/library boundary — see **[guest-libmvos.md](guest-libmvos.md)** (the
> current architecture, playable). Kept as historical record; the ABI/boot RE
> facts below remain accurate, but the *approach* is not current.

The chosen porting strategy for modern macOS (Apple Silicon). Decision: **no
OS-level emulation, no VM, keep `theocracy.real` byte-for-byte intact** → build
a bespoke user-mode emulator that executes the game's i386 code and
**high-level-emulates (HLE) the entire libmvos API boundary natively**
(SDL/Metal/CoreAudio). The same philosophy as a console emulator: the game
binary is sacred, the platform is reimplemented.

Why this boundary works (all confirmed by RE — see the contract inventory
below):

- The game links **only `libmvos.so` and libc** — every OS dependency (X11, OSS,
  pthreads, fork, sockets, CD, dlopen) sits *behind* the libmvos API.
- The game imports just **232 symbols**; libmvos exports 2400 with full
  GNU-v2-mangled signatures (types recoverable mechanically).
- **libmvos owns `main()`** (game imports it; libmvos file addr `0x951e0`,
  Ghidra `0xa51e0`). So *our native runtime is `main()`* — we control the entire
  boot sequence. It was later decompiled in full; the 10-step boot is in
  [../subsystems/application-bootstrap.md](../subsystems/application-bootstrap.md).

## Architecture

```
┌─────────────────────────────────────────────────┐
│ Host app (native arm64 macOS, C++/SDL)          │
│                                                 │
│  ELF loader ──▶ guest memory (low-4GB, 1:1)     │
│  Unicorn (i386 JIT) ◀─▶ trap layer at PLT/GOT   │
│  Native MVOS impl. (SDL video/audio/input,      │
│   filesystem, config, IPC, timers)              │
│  Callback bridge: native → emulated re-entry    │
│  Green-thread scheduler for guest cThreads      │
└─────────────────────────────────────────────────┘
```

1. **ELF loader** — map `theocracy.real` (ET_EXEC, base `0x08048000`), process
   `.rel.*`: PLT/GOT slots for the 232 imports → trap stubs; **R_386_COPY
   relocs** (`.rel.bss`) for framework globals the game references directly
   (`Intuition`, `VVC`, `SoundCard`, `IPCSystem`, `EnvSystem`, `TimerSystem`,
   MVOS vtables — see contract below). We never load the real libmvos/libc/X11.
2. **CPU** — Unicorn Engine (QEMU JIT as a library; runs on arm64 macOS). Target
   perf envelope is a Pentium 200–450 — trivially met.
3. **Guest memory** — shrink `__PAGEZERO`, map the guest's 4 GB 1:1 into low
   host memory (the 32-bit-Wine trick) so native code dereferences guest
   pointers directly, no marshaling copies. Bitmap pixel buffers must live in
   guest-visible memory (game code touches pixels directly; native blitters
   operate on the same buffers).
4. **HLE trap layer** — each imported symbol resolves to a unique trap address;
   on hit, read args off the emulated stack (GNU g++ 2.95 passes `this` as first
   stack arg — cdecl-like, no register thiscall) and dispatch to the native
   implementation.
5. **Callback bridge** — native → emulated calls for:
   `Init`/`Start`/`RollingDemoFrame`, virtual dispatch on game-side subclasses
   (vtables point into guest code), `cThread` bodies. One generic "call guest
   function(args) → result" primitive on Unicorn covers all of it.
6. **Threads** — guest `cThread`s (pthread on original) become **green threads**
   on one Unicorn instance (scheduler + native `cSemaphore`/`cPipe`). The
   sound-mixer thread is MVOS-internal → purely native, never emulated.

## The ABI contract (RE-confirmed inventory)

### Game → framework: 232 imports
~200 MVOS entry points + ~30 libc (`memcpy`, `sprintf`, `fopen`-family, pthread
mutex/key subset, `malloc/free`, `__builtin_new/vec_new`, `__strtod/__strtol`,
profiling stubs `monstartup`/`_mcleanup`). Full list: `objdump -T theocracy.real
| grep UND`. libc subset is small enough to HLE alongside MVOS.

### Framework → game: 348 exports from `theocracy.real`
The callback surface (`objdump -T theocracy.real | grep -v UND`):
- `Init__12cApplication` @ `0x8144600` — sets the 9 subsystem flags (all =
  1).
- `Start__12cApplicationiPPc` @ `0x8144650` — the whole game.
- `RollingDemoFrame__Fv` @ `0x8063540` — attract-mode hook.
- `cSprite` (`Init/Process/Lock/Unlock`), `cVODragBox`
  (`Paint/Process/Bound/GetXX/GetYY/Initialize`), `cProcess`
  (`Read/Write/Set/IsBlockMode`), `cStream::GetEventDescriptor`,
  `Set__10cDimensionUlUl`, `IncrementTo__t6cArray1ZPci` — vague-linkage/template
  code libmvos calls back into.
- **64 `__vt_*` vtables** + `__ti*` typeinfo. Ones in game `.data`
  (`0x84cf...`–`0x84d3...`) are game-defined; ones at `0x08598...` (`.bss`) are
  **R_386_COPY relocations of libmvos vtables** — the loader must fill these
  from our native side (i.e. our HLE must provide vtable *data* whose function
  pointers are trap addresses, at stable guest addresses).
- Plus C++ runtime (exception/RTTI machinery) — game carries its own copy in
  `.text`; runs emulated, no HLE needed.

### Boot sequence (replaces libmvos `main` — decompiled, confirmed)
Mirror libmvos `main()` exactly (details + addresses in
[../subsystems/application-bootstrap.md](../subsystems/application-bootstrap.md)):

1. Load ELF, apply relocs, install traps and copy-reloc data.
2. Run game `.ctors` (`0x8597400`–`0x8597763`) under emulation.
3. Native: ignore SIGPIPE (n/a on our side), attach MessagePort, **parse
   `mvos.cfg` into our EnvSystem** (`IdentifyFileSystemMvosCfg`).
4. Call `Init__12cApplication` (guest) → read the 9 flags (Sound, Video, Mouse,
   Keyboard, Redbook, Network, Pointer, Timer, Intuition — Theocracy sets all).
5. Read `vmachine/fillobjmem` → `Objmem_Fill`. **Consequence: the game never
   relies on zero-init allocations** (default fill=poison), so our HLE allocator
   can skip zeroing.
6. `OpenSubsystems`: bring up native video (→ publish
   `VVC`/`VKeyboard`/`VMouse`/`SystemPointer`), then Redbook→`VCD`,
   Timer→`TimerSystem`, Network→IPC, Sound→`SoundCard` — each at its
   copy-reloc'd guest singleton address, gated by the flags.
7. If Intuition flag: construct `cIntuition` → global `Intuition`.
8. Call `Start__12cApplicationiPPc(argc, argv)` (guest) — game runs until it
   returns.
9. Destroy `Intuition`, then `CloseSubsystems` (TimerSystem, VVC, VKeyboard,
   VMouse, SystemPointer, VCD, SoundCard), cleanup, return.

Note: the four input/video device globals are all produced by the *video*
`LoadDevicePlugins` step (the `_x` plugin family), not separate open calls.

## What each native subsystem must honor (from RE)

| Subsystem | Contract source | Native impl |
|-----------|----------------|-------------|
| Video | [vvc_x-backend.md](vvc_x-backend.md): accept mode 5 (16bpp)/4 (15bpp); dirty-rect present via `cGD::Refresh`; `ShowBuffer` is a **no-op**; `mvos.cfg` `[vmachine] fullscreen` | RGB565/555 buffer in guest mem → SDL texture. Depth restriction disappears. |
| Audio | `cSoundCard_Linux`: OSS `/dev/dsp`, 22050→11025 Hz, 16→8-bit, stereo probe, software mixer on own thread | SDL/CoreAudio callback pulling from native `cSoundCard_SoftwareMix`. |
| Input | vvc_x `ProcessEvents` → `cKeyboard::ConvertRawkey/PushKey/ReleaseAll`, `cMouse/cPointer::EVENT_*`; game polls via `GetIMouseButtons`/`KeyMatrix`/`RawkeyToAscii` | SDL events → native keyboard/mouse objects. `ReleaseAll` on focus loss. Hide host cursor (game draws its own sprite). |
| Threads | `cThread::Launch` = `pthread_create(Entry)` + `cPipe`; game subclasses run guest code | Green threads; primitives native. Census of game-side `cThread` subclasses TODO. |
| Processes | `cTask::Launch` = `fork`+`execlp` (spawns `theoserver`) | Stub initially; later spawn native/emulated server. |
| Network | `cIPCSystem` TCP/IP (+IPX, serial) ; game grabs `localhost:5043` as single-instance lock in `Start` | Native BSD sockets; the port-5043 lock just works (or fake it). IPX/serial: skip. |
| Movies | `PlayMovie` → `External_PlayAnim` → SMPEG in libmvos (`ubi_logo.mpg`, `logo.mpg`, `intro.mpg`) | Native MPEG-1 decode (smpeg-philos source in `linux/smpeg-philos.tgz` is the reference; or ffmpeg/AVFoundation). |
| CD audio | `Redbook` flag; `cVCD::Play(track)`, `/dev/cdrom` | Play ripped track files; stub OK at first. |
| Timer | SIGALRM heartbeat (`setitimer`) drives `cTimerSystem`/`cVTimer` ticks (Intuition registers as `cVTimer`, "Unable to activate timer" if it can't) | Host timer → scheduled guest callbacks/native timer objects. |
| Config/log | `mvos.cfg` (`cEnvSystem` classes/vars, e.g. `vmachine/fullscreen`), `mvos.log`, `data/selap.txt` | Native INI-ish parser, same key vocabulary. |
| Memory mgr | `cSystemMemory` 32 MB budget, evictable `cMemBlock`s ([memory-and-containers.md](../subsystems/memory-and-containers.md)) | Native, but **blocks allocated in guest-visible memory**; layouts must match (game inlines accessors — offsets are baked into game code). |

## Hard parts / risks

> **Written as predictions, before anything ran. Three of the five are now
> settled, and are marked inline.** Kept in their original form because the
> corrections are the interesting part — this is the risk list that a plausible
> argument produced, against what building the thing actually found.

1. **Inlined accessors bake MVOS object layouts into the game.** Any MVOS object
   the game touches directly (cString 32-byte block, cNode/cList links,
   cMemBlock fields, cVVC layout, sInput, cRectangle/cDimension/cColor PODs…)
   must match original layout exactly. Recover layouts from ctors in Ghidra
   (several already documented in `structs/` and memory doc).
2. **Native→guest virtual dispatch.** Our native code must call through *guest*
   vtables when an object is game-subclassed, and through native paths when it's
   ours. Convention: HLE objects carry guest-visible vtables whose slots are
   trap addresses; game-subclassed objects carry guest vtables naturally.
   Dispatch = always read the vtable from guest memory and call whatever's there
   (trap → native, code addr → Unicorn).
3. **Green-thread correctness** — blocking primitives (`cSemaphore::Lock`, pipe
   reads, `Sleep`) are the yield points; guest code presumably assumes
   preemption. Audit which game threads exist before deciding whether
   cooperative-with-forced-preemption (Unicorn timeout hook) is needed. —
   **Settled: no preemption is needed.** The guest's threads are the sound mixer
   and the timer, both green-run inside the emulation, and the port has exactly
   one real host thread (the watchdog). See
   [host-architecture.md](host-architecture.md), "The green run".
4. **g++ 2.95 exception handling** runs entirely inside guest code (game carries
   its own sjlj runtime) — should Just Work, but `Fatal` paths that unwind
   across the HLE boundary would not. `Fatal` doesn't return, so treat it as
   terminate-with-message. — **Settled: it did Just Work**, and no unwind ever
   crossed the boundary.
5. **The 5.6 MB unknown**: game logic may do things we haven't seen (direct
   `/proc` reads etc.). Mitigated by: syscalls can't happen except through
   imports (all trapped) — any surprise shows up as a trap we haven't
   implemented yet, loudly. — **Settled: there was no surprise.** The mitigation
   was the right one and it never had to fire: a full boot into gameplay reports
   **0 unimplemented traps**, over 3.58M import calls in a 6-minute run.

## Milestones

- **M0** — Phase 0 API inventory + headers. ✅ **DONE** — GNU-v2 demangler
  (`tools/gnuv2_demangle.py`, 100% of 2400 libmvos exports), structured
  inventory (`data/mvos_api.json`: 252 classes / 1304 methods / 131
  polymorphic), 232-symbol HLE boundary (`data/game_imports.tsv`: 191 MVOS calls
  into 53 classes + 41 libc), and the address-annotated signature header
  `include/mvos_api.hpp`. Regenerate all via `sh tools/regen_api.sh`. See
  [../reference/mvos-api-inventory.md](../reference/mvos-api-inventory.md).
- **M1** — Loader + Unicorn bring-up: map ELF, run `.ctors`, call `Init`, read
  the 9 flags, print them. **No game data needed.** ✅ **DONE** — `port/` (C++17
  + Unicorn 2): maps the two PT_LOAD segments, traps all 232 imports (95 more
  JMP_SLOT/GLOB_DAT resolve to game-local exports), runs 215 `.ctors` under
  emulation (213 clean, 2 fault on unimplemented data-descriptor ctors), calls
  `Init__12cApplication`, and all 9 subsystem flags transition `0 → 1`. The
  game's own code runs (emits its own `printf` output). Details + M2 worklist:
  [m1-loader.md](m1-loader.md).
- **M2** — Enough HLE to reach the menu: file I/O + `cSystemMemory` +
  strings/containers + video + input + config. First pixels. **Needs CD data.**
- **M3** — Audio, timers, sim loop → playable single-player.
- **M4** — Movies, CD audio, save/load, fullscreen polish → shippable Mac app.
- **M5 (optional, long-term)** — incremental native lift: hook individual game
  functions, replace with native C++ verified against emulated originals →
  gradually a true native port (devilutionX-style, but with a running product
  from day one).

## Open items — all resolved or made moot

Every one of these is settled. Listed with its outcome, because **this is the
list that stops dead work being re-proposed**: three of them were questions
*only* because this plan needed them, and would become live again only if the
project went back to native-replacing the engine wholesale.

| Item | Outcome |
|---|---|
| Decompile libmvos `main` | **Done** — the 10-step boot is in [../subsystems/application-bootstrap.md](../subsystems/application-bootstrap.md). |
| `LoadDevicePlugins` (`0xa4990`) internals | **Still open**, and now owned by [../subsystems/application-bootstrap.md](../subsystems/application-bootstrap.md). Not needed by the port: the `dlopen`/`dlsym` handshake is synthesised by the trap layer. |
| `eBMType` enum — full value set (2/4/5/6/7 from vvc_x; 6 vs 7 unknown) | **Moot.** Only needed to author bitmap ctors natively; the engine's own ctors run. |
| Census of game-side `cThread` subclasses | **Moot.** It sized a native green-thread scheduler; the guest's own threading runs under the HLE `cThread`/pipe shim. |
| MVOS object layouts the game inlines — a systematic pass | **Moot.** It was the layout-compatibility surface for hand-built HLE objects; the objects are real now, so the layouts are too. (`cString`/`cNode`/`cMemBlock` were done anyway — [../subsystems/memory-and-containers.md](../subsystems/memory-and-containers.md).) |
| Game data acquisition; RE `inst.linux` to write an extractor | **Done, and the installer never had to be reversed** — the pack format was cracked directly. See [../reference/phls-format.md](../reference/phls-format.md). |
| The ~30 libc imports: exact list + semantics audit | **Done** — implemented in the HLE OS shim; a full boot into gameplay reports **0 unimplemented traps**. |

The X11-plugin *port gotchas* this plan also carried (match the X visual depth;
set `LD_LIBRARY_PATH` for the `RTLD_LAZY` relative-name `dlopen`) are moot for
the same reason: the X11 plugin was replaced wholesale by the SDL backend.
