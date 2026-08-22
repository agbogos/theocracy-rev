# vvc_x — the X11 video/input backend (porting notes)

`vvc_x` is one of the game's `dlopen`'d display plugins. It's a small (~27 KB
`.text`), unstripped GCC 2.x shared object that turns the engine's abstract
display into an **X11 window presented via MIT-SHM**. This is the reference
implementation for any modern port — it fully specifies the backend seam.

> **Still current as a reference.** Under the [guest-libmvos](guest-libmvos.md)
> architecture we don't run this plugin: real libmvos's `dlopen`/`dlsym` are
> HLE'd (synthetic device objects), and `OpenDisplay`/`SwapBuffers` are trapped
> to a native SDL RGB565 backend (`port/src/video.cpp`). The seam below is what
> those traps implement.

## Backend seam (the whole replaceable surface)
Two layers, both small:

**1. `cGD_X8` / `cGD_X15` / `cGD_X16` / `cGD_X32`** — per-depth GD backends that **extend the engine's `cGD_LFB8/15/16/32`** (linear framebuffer) and override only:
- `ctor(cDimension&, ulong)`, `dtor`
- `Refresh(cRectangle&)` — **the present path**: `XShmPutImage(dirtyRect)` +
  `XSync`.
- `IsAsyncRefreshCapable()`

So the engine does all pixel work in `cGD_LFB*`; the backend only **presents**.

**2. `XDriver_*` C API** (the X11 glue):
| Function | Role |
|----------|------|
| `XDriver_Setup()` | `XOpenDisplay(getenv("DISPLAY") ?: ":0")`, read screen depth, make a blank cursor |
| `XDriver_CreateWindow(w,h)` | `XCreateSimpleWindow`; optional borderless fullscreen via `_MOTIF_WM_HINTS` (gated by env `vmachine/fullscreen`); `XSelectInput(0x20007f)` = key±/button±/motion/expose/structure; `XMapWindow` |
| `XDriver_GetVMemAddr()` | **returns the MIT-SHM XImage data ptr = the framebuffer the engine renders into** |
| `XDriver_GetWidth()` | stride/width |
| `XDriver_ChangeResolution(w,h)` | `XF86VidModeSwitchToMode` (fullscreen only) |
| `XDriver_GrabPointer()` / `UngrabPointer()` | `XGrabPointer` / `XUngrabPointer` |
| (`XDriver_SetFullScreen`, `XDriver_DestroyWindow`, `XDriver_GetDisplay/Window/GC` internal) | |

## Render model (one paragraph)
On init the driver allocates a **shared-memory `XImage`**
(`XShmCreateImage`+`shmget`+`shmat`+`XShmAttach`). `XDriver_GetVMemAddr` hands
the engine that buffer's address; the engine's `cGD_LFB*` blits everything into
it. To display a frame, `cGD_X*::Refresh(rect)` calls `XShmPutImage` for the
dirty rectangle and `XSync`. That's it — **the framebuffer pointer + a
dirty-rect present is the entire contract.**

## Input
`vvc_x` also pumps X input (`ProcessEvents` @ `0x19980`, drains `XEventsQueued` fully each call), translating events straight into engine calls on the libmvos globals `_VKeyboard` / `_VMouse` / `_SystemPointer` (these are the exact entry points a replacement must call):
- KeyPress/KeyRelease: `XLookupKeysym` → `cKeyboard::ConvertRawkey(keysym,
  isDown)` → `PushKey(eKeyCode, bool)`.
- ButtonPress/Release: X buttons 1/3/2 → engine bitmask bits 0/1/2
  (left/right/middle) → `EVENT_Buttons(uchar)` on **both** `cMouse` and
  `cPointer`.
- MotionNotify: `EVENT_Move(tPoint&)` on both.
- EnterNotify: re-grab pointer if grab requested; LeaveNotify: ungrab
  bookkeeping.
- **FocusOut → `cKeyboard::ReleaseAll`** (no stuck keys on alt-tab) — replicate
  this.

## Modern-Linux compatibility verdict
| Dependency | Modern status |
|-----------|---------------|
| X11 core (Xlib) | ✅ native Xorg or XWayland |
| **MIT-SHM** (`XShm*`, `shm*`) | ✅ supported locally on Xorg/XWayland |
| Motif hints (fullscreen) | ✅ honored/ignorable |
| **XF86VidMode** (`XDriver_ChangeResolution`) | ⚠️ **only deprecated dep** — fullscreen resolution switch. Windowed mode never calls it. |

**Bottom line:** the graphics/input path is standard X11 + MIT-SHM and should
run on modern Linux essentially unchanged. No SVGAlib, no root, no raw hardware.
(The earlier "SVGAlib is the scary part" was only true of a *different* backend;
the shipped `vvc_x` sidesteps it.)

## Two porting paths (historical — neither is what happened)

> Recorded as the decision point it was. The project took a **third** option that
> only exists because the engine itself runs emulated: libmvos's `dlopen`/`dlsym`
> are HLE'd to hand back synthetic device objects, and `OpenDisplay` /
> `SwapBuffers` are trapped to a native SDL RGB565 backend — so the plugin family
> below is neither run nor reimplemented, it is *replaced at the seam*. The two
> options here still describe what a native-Linux revival would face.

1. **Run the real `vvc_x` as-is (weekend-scale):** provide a 32-bit userland —
   `libX11`, `libXext` (MIT-SHM), `libXxf86vm`, plus the era's
   `libstdc++`/`libg++` bundled with the game — run under X (native/XWayland),
   use **windowed** mode to dodge XF86VidMode. Likely the fastest route to first
   pixels.
2. **Thin SDL2 backend (clean/64-bit, weeks-scale):** reimplement the ~8
   `XDriver_*` functions + 4 `cGD_X*::Refresh` overrides. `GetVMemAddr` → return
   a `malloc`'d buffer; `Refresh(rect)` →
   `SDL_UpdateTexture`+`SDL_RenderCopy`+present; feed SDL input to the same
   `cKeyboard`/`cMouse`/`cPointer` entry points listed above. Everything else
   (the engine) is reused unchanged.

## Plugin load handshake (resolved)
The plugin exposes two **unmangled `extern "C"`** symbols — these are the `dlsym` targets:
- **`QueryDevice()`** → returns `1` — capability probe ("is this backend
  usable?").
- **`CreateVideoDevice()`** → `new cVVC_Linux_X`, sets its vtable, calls
  `XDriver_Setup()` (`XOpenDisplay`), returns it as a **`cVVC*`**.

So the engine side (libmvos, via `cLibrary`) is: `dlopen("vvc_x…") →
dlsym("QueryDevice")` probe → `dlsym("CreateVideoDevice")` → use the returned
`cVVC` as its video singleton, driving it through virtuals `SetVideoMode` /
`SetPalette_Real` / `ShowBuffer` / `WaitVBlank`.

**Decompile-confirmed: `ShowBuffer(uchar)` and `WaitVBlank` are literal no-ops
in `vvc_x`** (`0x19f80` / `0x19f90`, empty bodies). There is no page-flip; the
*only* present path is `cGD_X*::Refresh(dirtyRect)` → `XShmPutImage` + `XSync`.
Double-buffering is engine-side (`cVVC` front/back GDs); an SDL replacement only
needs Refresh → texture-update → present.

**`cVVC_Linux_X`** (0x2c bytes): `[0]` front `cGD_X*`, `[1]` back-buffer GD,
`[2]` offscreen `cBitmap`, `[7]` depth code, `[8]` w, `[9]` h, `[10]` vtable.

`SetVideoMode(cVModeRequest{w,h,depthCode})` maps depth code → backend:

| depthCode | requires X visual depth | backend | game usage |
|-----------|------------------------|---------|-----------|
| 2 | ≥ 8 | `cGD_X16` | — |
| 4 | **15** | `cGD_X15` | SP fallback |
| 5 | **16** | `cGD_X16` | SP primary |
| 6 / 7 | **24** | `cGD_X32` | — |
| 0 / 8 | — | unsupported (returns 0) | — |

## ⚠️ Color-depth gotcha (the key practical finding)
`SetVideoMode` **requires the X server's actual visual depth to match the
requested mode.** The game asks for **mode 5 (16-bit) then mode 4 (15-bit)**
(seen in `cApplication::Start` / `PlayMovie`). Modern X servers run at **depth
24**, so both requests fail → `OpenDisplay` returns 0 → black screen. This is
almost certainly why it runs under **Debian Woody** (whose X defaulted to 16
bpp) but would fail on a stock modern X server.

**Fixes, easiest first:**
1. Run a **16-bit X**: `Xephyr -screen 800x600x16 :1` then `DISPLAY=:1
   ./theocracy` (or Xvfb/xserver at 16 bpp). Zero code changes — the recommended
   first attempt on a modern host.
2. Patch the depth check in `SetVideoMode` to accept depth 24 + add a 16→32 bpp
   conversion in `Refresh`.
3. In an SDL2 shim, this disappears — you own the framebuffer and convert 16-bit
   → the texture format yourself.

## Engine-side loader (confirmed — `LoadDevicePlugins`, libmvos `0xa4990`)

> **Address corrected (audit 2026-07-26).** This doc previously cited `0xa49a0`.
> The true entry is **`0xa4990`** (file `0x94990`): it is the address
> `OpenSubsystems` actually calls (`0xa4f73`), and it begins with the
> "already loaded?" guard `CMP [0xd6b80],0 / JNZ`. `0xa49a0` is only a
> fragment start — no callers, a single `.eh_frame` DATA xref. The Ghidra DB
> had the label on the wrong address too; fixed (`LoadDevicePlugins` /
> `LoadDevicePlugins_cont`).
The engine loads each device family the same way, gated by the `cApplication::Required` flags:

```c
handle = dlopen(path, RTLD_LAZY);   // failure -> dlerror() + "Can't open lib because %s"
cLib::MapAllSymbol();               // dlsym(h,"Create<Type>Device") + dlsym(h,"QueryDevice")
if (!QueryDevice()) return 0;       // probe
global = CreateXDevice();           // factory -> VVC / VKeyboard / VMouse / VPointer
```

Hardcoded default plugin paths (the `_x` = X11 family), each wrapped by a
`cLib*` class:

| Family | Default `.so` | Wrapper | Global |
|--------|--------------|---------|--------|
| Video | **`libmvos_vvc_x.so`** | `cLibVVC` | `VVC` |
| Video (alt) | **`libmvos_vvc_glide.so`** | `cLibVVC` | — (3dfx Glide) |
| Keyboard | `libmvos_keyboard_x.so` | `cLibKeyboard` | `VKeyboard` |
| Mouse | `libmvos_mouse_x.so` | `cLibMouse` | `VMouse` |
| Pointer | `libmvos_pointer_x.so` | `cLibPointer` | `VPointer` |

Video-device **selection** (caller `FUN_000a4fae`): reads a configured device
name; default is **`xf86`** (→ `libmvos_vvc_x.so`), alternative `glide`. So the
input/mouse/pointer are **separate plugins** from `vvc_x` (same
`Create*Device`/`QueryDevice` ABI), not duplicated inside it — `vvc_x` owns the
X window/present + event pump; the input plugins are the device objects.

**Port note:** plugins are `dlopen`'d by **relative name with `RTLD_LAZY`** →
they must be reachable via `LD_LIBRARY_PATH` / rpath / cwd. Point
`LD_LIBRARY_PATH` at the game's lib dir. The whole X11 device family (`*_x.so`)
is what you run or replace.

## Open threads

Archaeology. The port only ever runs at 16bpp, so none of this is a gap in it.

- **The other `cGD` depth backends** — `_LFB8` / `_LFB15` / `_LFB24` / `_LFB32`,
  and how the vtable selects between them. LFB16 is done: the blit family is
  transliterated byte-exact into `port/src/blit.cpp` (file offsets `0x5c4e0`,
  `0x5c940`, `0x5c9b0`, `0x5cb70`, `0x5cbb0` — add `0x10000` for Ghidra),
  including the RLE packet format (`[count][flag]`, `flag == 0` ⇒ transparent
  run, else palette indices with index 0 as a hole) and the
  cdecl-despite-`__regparm` convention.
- **The bitmap / palette pipeline** around those backends.
