# Application Bootstrap & Platform Integration

How the engine comes alive, and what it sits on. All addresses in `libmvos.so`.

## The split: framework here, main loop in the game binary

`cApplication` in libmvos is a **framework base class**. The game's run loop
lives in the game executable, but — **correction to the earlier read** — the
orchestrator does not: **`main()` itself is exported by libmvos** (file
`0x951e0` / Ghidra `0xa51e0`, 0xfc bytes; the game binary *imports* `main` and
its `main@0x804fb44` is just a PLT thunk). AmigaOS-style inversion: the
framework owns startup, the app provides `Init`/`Start` callbacks. libmvos
provides:

- The **requirement contract** — static flags declaring which subsystems an app
  needs (set directly by the game's exported `Init__12cApplication`, which
  assigns all nine `= 1`; the `XxxRequired()` setters exist but Theocracy's
  `Init` writes the globals directly).
- The **subsystem singletons + their Open/Close entry points**, driven by
  `main()` after `Init()`.

Every `cApplication` accessor's only xref is `From Entry Point [EXTERNAL]`, i.e.
called from the game binary.

## libmvos `main()` — the exact boot sequence (decompiled, Ghidra `0xa51e0`)

Confirmed. This is the function our native HLE runtime replaces verbatim (steps
4 and 8 are calls into emulated game code; everything else becomes native):

1. `signal(SIGPIPE=13, SIG_IGN)` — ignore SIGPIPE (the socket/pipe IPC layer
   relies on this).
2. `MessagePort = cPipe::UsePipe("MVOSMessagePort")` — attach the global
   message port.
3. `IdentifyFileSystemMvosCfg()` (`0xa4640`) — locate & parse `mvos.cfg`
   into `EnvSystem` (config is available from here on).
4. `cApplication::Init()` — *game callback*; sets the 9 requirement flags
   (Theocracy: all 1).
5. **Read `EnvSystem` class `"vmachine"` var `"fillobjmem"`** → global
   `Objmem_Fill` (default 1; value `"n"` → 0). `Objmem_Fill=1` means
   **freshly-allocated object memory is poison-filled**, so game code does *not*
   rely on zero-initialized allocations — **our HLE allocator need not zero
   memory.**
6. `OpenSubsystems()` (`0xa4f20`) — construct device plugins + subsystems
   (order below).
7. **If `Application::Intuition` flag:** `new cIntuition(0xb4)` → global
   `Intuition`.
8. `cApplication::Start(argc, argv)` — *game callback*; the entire game runs
   inside this call.
9. **Destroy `Intuition`** (vcall slot `+8`, arg `3` = delete-mode) — *before*
   subsystem teardown.
10. `CloseSubsystems()` (`0xa50e0`); final cleanup; `return 0`.

### `OpenSubsystems()` construction order (`0xa4fae`)
Each gated by its flag (read after `Init()`):
1. **Video device** — `GetVideoDeviceName()` reads `mvos.cfg` `vmachine/video`
   (**not** `device` — corrected 2026-08-15 from the decompiled body, which
   passes the literal `"video"` to `cEnvClass::FindVariable`); name → loader.
   **Default (unset) = `"xf86"`** → `LoadDevicePlugins` (`0xa4990`), which
   dlopens the whole `libmvos_*_x.so` family and creates globals **`VVC`,
   `VKeyboard`, `VMouse`, `SystemPointer`** (so keyboard/mouse/pointer come from
   *here*, not separate steps). Alt names → `glide` (`0xa4ce0`) / a third
   (`0xa4910`); unknown → `Fatal("Unknown video device")`. Prints `"Useing
   default video device xf86."` (sic).
2. **Redbook** flag → `new(0x14)` → `VCD` (CD audio).
3. **Timer** flag → `new(0x20)` → `TimerSystem`.
4. **Network** flag → `new(4)` → IPC/network holder.
5. **Sound** flag → `GetSoundDeviceName()`; `"no"` → `new(4)` dummy card; else
   `new(0x5c)` + `cSoundCard_Linux` ctor (`0xa2ba0`, see
   [platform-audio-threads.md](platform-audio-threads.md)).

`CloseSubsystems()` (`0xa50e0`) tears down in fixed order: `TimerSystem`, `VVC`,
`VKeyboard`, `VMouse`, `SystemPointer`, `VCD`, `SoundCard`.

**Config vocabulary — corrected and closed 2026-08-15.** The earlier version of
this line listed vars `device` and `fullscreen`, and both were wrong. libmvos
reads **exactly five** keys, all in class `vmachine`, each via
`FindClass("vmachine")` → `FindVariable(<key>)` with a hardcoded fallback:

| Key | Reader | Default when absent |
|---|---|---|
| `soundcard` | `GetSoundDeviceName` (`0xa4880`) | `"/dev/dsp"` (value `"no"` → dummy card) |
| `video` | `GetVideoDeviceName` (`0xa48c0`) | returns 0 → `"xf86"` |
| `cdrom_device` | `GetCDRomDeviceName` (`0xa4840`) | `"/dev/cdrom"` |
| `cdrom_mountpoint` | `VM_GetCDRomName` (`0xa52e0`) | `"/mnt/cdrom"` |
| `fillobjmem` | boot step 5 (`0xa5210`) | fill on; **only a leading `'n'` clears it** |

There is no `device` key and **no `fullscreen` key**: the literal string
`fullscreen` does not occur anywhere in `libmvos.so` *or* `theocracy.real`,
though the installer writes the line. The only other key in a shipped `mvos.cfg`
is `[game] language`, which the **game** reads (to build
`data/locale/<language>.sdb`), not the engine. Established from
[reconf-tool.md](../reference/reconf-tool.md).

### Requirement flags (`cApplication`, ~`0x579e0`–`0x57b00`)
Static globals (`Video`, `Sound`, `Mouse`, `Pointer`, `Keyboard`, `Redbook`, `Timer`, `Network`, `Intuition`), each with:
- setter `XxxRequired()` → sets flag to 1 (e.g. `VideoRequired` @ `0x57af0`).
- getter `IsXxxRequired()` → returns flag (e.g. `IsVideoRequired` @ `0x57a50`).

The game's `cApplication` subclass declares its needs (e.g. `VideoRequired();
SoundRequired(); MouseRequired();`), and the run loop opens only those
subsystems. `Redbook` = CD audio; `Intuition` = the Amiga-style UI/windowing
layer.

## Library load — global constructors

`entry` / `_init` (`0x52980` / `0x521e0`): standard g++ `.so` startup — walks
the `.ctors` table (`PTR_PTR_000bee60`) running each global constructor, then
`__register_frame_info`. The global ctors build engine static state:

- `keyed.to.VVC` (`0x970e0`): inits `VModeInfo` (a `cDimension`) to 0×0 — the
  video controller's mode state.
- `keyed.to.Intuition` (`0x9ea10`): sets up a global `cFile inputfile` for
  `input0.data` and calls `IdentifyFileSystem` — the input/UI layer loads an
  input config (keymap/bindings) at load.
- Others: `keyed.to.cPalette`, `keyed.to.cEnvVar`, `keyed.to.A` (audio),
  `keyed.to.cString`, `keyed.to.Create`, `keyed.to.OpenR`.

## Video bring-up — `cVVC::OpenDisplay(cVModeRequest&)` (`0x95ce0`)

`cVVC` is the video/screen controller. `OpenDisplay`:
1. Sets `NoTimerInterruptPaintFlag = 1` to suppress SIGALRM-driven repaints
   during the switch.
2. Compares requested mode (w `+0x20`, h `+0x24`, depth `+0x1c`) to current; if
   changed, calls the **backend mode-setter** through a vtable (`+0x28 → +0xc`).
3. On success, initializes **two screen/GD objects** (`this+0` = front, `this+4`
   = back → **double buffering**) via their vtables, then `SetBuffers()`.
4. Clears `NoTimerInterruptPaintFlag`.

The concrete pixel backend (`cGD_LFB8/15/16/24/32`, etc.) is reached via vtable
— selected/loaded at runtime (see below), not hard-linked.

## Platform layer — what libmvos links against

From the dynamic imports:

| Dependency | Evidence | Meaning |
|-----------|----------|---------|
| **SMPEG** | `SMPEG_new/playvideoframe/enablevideo/enableaudio/setdisplay/setvolume/status/error/delete` | MPEG **full-motion video** (cutscenes). Distinct from the in-house FLC player (FLC = in-engine anim, SMPEG = movies). |
| **libdl** | `dlopen/dlsym/dlclose/dlerror` | Backends loaded at **runtime as plugins**. |
| **Graphics/mouse backend** | `SVGALIB_Init` / `SVGAMOUSE_Init` are **empty stub hooks**; **no direct `vga_`/X11 imports** | Backends are dlopen'd plugins, not statically bound. **The shipped family is X11** (`libmvos_*_x.so` — see [porting/vvc_x-backend.md](../porting/vvc_x-backend.md)); the SVGAlib hooks are vestigial (that backend was never shipped, like the `glide` one). |
| **pthreads + POSIX sem** | `pthread_create/join/cancel`, `sem_init/wait/post/...` | Threading: sound server/recorder, timers, IPC. |
| **BSD sockets** | `socket/bind/connect/accept/listen/send/recv/sendto/recvfrom/select/gethostbyname/htons/ntohs` | TCP/IP multiplayer (plus in-house IPX). |
| **Interval timer** | `setitimer/signal/sigaction` | SIGALRM heartbeat → drives scheduling/repaint (`NoTimerInterruptPaintFlag`). |
| **Process control** | `fork/execlp/waitpid/kill` | `cTask::Launch` (@ `0xa5740`) = pipe + `fork` + `execlp` — child-process spawning (the in-game multiplayer-server launch; the readme.linux "glibc bug" lives here). See [platform-audio-threads.md](platform-audio-threads.md). |
| libc | file/str/malloc/`__builtin_new`/`__builtin_vec_new` | Note: mixes libc `malloc` with C++ `new[]` (the latter backs `cSystemMemory`). |

**Bidirectional game↔engine coupling:** libmvos *imports* symbols from the game
binary — `cDisplay`, `Init`, `Start`, `RollingDemoFrame`. So the two are
mutually linked; the engine calls back up into game-provided code (e.g.
`RollingDemoFrame` = attract/demo-mode callback).

## Bootstrap sequence (reconstructed)

1. **`.so` load** → `_init` runs global ctors → subsystem static state built
   (video mode info, input config, palette, env, audio).
2. **libmvos `main()`** (Ghidra `0xa51e0`) calls the game's exported
   `Init__12cApplication` → the nine requirement flags are set (Theocracy: all).
3. **libmvos `main()`** opens each required subsystem (dlopen device plugins →
   `VVC`/`VKeyboard`/`VMouse`/`VPointer` globals, sound card + server,
   timer/SIGALRM, Intuition, network), then calls the game's
   `Start__12cApplicationiPPc` — the whole game runs inside that call. Video
   mode itself is set later, from game code, via `cVVC::OpenDisplay(mode)`.
4. **Frame loop** (in game binary) pumps events → updates → paints the back
   buffer → flips. Timer SIGALRM triggers periodic repaints unless
   `NoTimerInterruptPaintFlag` is set.
5. Shutdown reverses it (`.dtors`, subsystem Close).

## Open threads
- ~~Decompile `main` @ `0xa51e0`~~ **DONE** — see the boot-sequence section
  above.
- `LoadDevicePlugins` (`0xa4990`) internals — confirm it creates all four device
  globals (`VVC`/`VKeyboard`/`VMouse`/`SystemPointer`) and how the input
  plugins' `Create*Device`/`QueryDevice` are resolved (should mirror the video
  handshake in [porting/vvc_x-backend.md](../porting/vvc_x-backend.md)).
- `IdentifyFileSystemMvosCfg` (`0xa4640`) — where it searches for `mvos.cfg`
  (cwd `~/.theocracy` per launcher) and the full `EnvSystem` class/var schema.
- ~~Trace how a graphics backend is chosen and `dlopen`'d~~ **RESOLVED** —
  `LoadDevicePlugins` / `Create*Device` ABI, see
  [porting/vvc_x-backend.md](../porting/vvc_x-backend.md).
- ~~Switch to the game binary to read the concrete `cApplication::Run`~~
  **RESOLVED** — there is no game-side `Run`; the game's frame loop is
  `RealmGameLoop` etc. inside `Start` (see
  [game-flow-and-main-loop.md](game-flow-and-main-loop.md)).
- `RollingDemoFrame` / `RollingDemo*` — attract-mode hook provided by the game;
  where is it driven from in libmvos?
