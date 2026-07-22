# M2 core — native MVOS layer (in progress)

> **⚡ SUPERSEDED (2026-07-22) — pivoted to guest-libmvos.** M2 was the pure-HLE
> effort to hand-reimplement libmvos natively (vtable synthesis, singleton
> wiring, `cTextFile`, `sscanf`, the render-boundary imports, `PaintTree` /
> `CalcAbsCoordTree`). It reached a live render loop but proved the GUI surface
> unbounded. The project pivoted to running **real libmvos** under Unicorn —
> see **[guest-libmvos.md](guest-libmvos.md)**. `port/src/mvos.cpp` /
> `video.cpp`'s render code is left in tree but **not linked** (CMake comments
> `mvos.cpp` out). Kept as historical record + a source of RE'd struct layouts;
> the approach is not current.

M2 = enough native HLE to reach the menu. This doc tracks the **core
infrastructure** landed so far in [`port/`](../../port), on top of the
[M1 loader](m1-loader.md). The through-line: replace M1's logging stubs with
native MVOS objects that operate on byte-exact guest-memory layouts, and stand
up native↔guest **virtual dispatch** (the plan's hard-part #2).

## Landed this increment

**Multi-region trap layer** (`machine.{hpp,cpp}`). `add_code_traps(base, n, fn)`
registers any number of scoped `UC_HOOK_CODE` windows; the import boundary
(`TRAP_BASE`) and the synthesized vtables (`VT_TRAP_BASE`) are two such regions.
Added an invalid-memory hook so faults report **`eip` + faulting address**
(`call()` now throws e.g. `READ_UNMAPPED at eip=0x816cdeb accessing 0x8`).

**Vtable synthesis + singleton wiring** (`mvos.{hpp,cpp}`, `Mvos::apply_copyrelocs`).
The 79 `R_386_COPY` relocs (previously skipped in M1) are now processed:
- **34 `__vt_*` vtables (157 slots total)** — each slot is filled with a unique
  trap address in `VT_TRAP_BASE`, so a guest virtual call lands in
  `dispatch_vtable()` (logs `[vtable] TODO <name>[<slot>]`, the discovery log for
  virtual methods, mirroring the import TODO log).
- **10 pointer singletons** (`SystemMemory`, `Intuition`, `IPCSystem`, `VCD`,
  `VVC`, `SoundCard`, `VKeyboard`, `VMouse`, `LocaleDataBase`, `RandomServer`) —
  each backed by a zeroed guest object so a deref reads zeros, not a fault.
- 35 other COPY targets (flags, palettes, typeinfo, inline `EnvSystem`) left zero.

**First native MVOS handlers** (registered via `TrapLayer::register_handler`):
`cData_Bitmap`/`cData_AnimBitmap` ctors (construct an empty lazy descriptor with
a valid payload vtable), `cMemBlock_::IsValid`, `cAnimBitmap::GetBoundingBox`.
Object model confirmed from Ghidra: **`cData_Bitmap : cMemBlock`** (offsets
`+0x0c` data / `+0x14` lock / `+0x1c` payload-vtable line up with
[memory-and-containers.md](../subsystems/memory-and-containers.md)); the caller's
`if (!data && !IsValid) (*(vtbl+8))(this)` idiom is the **lazy-load hook**
(payload vtable slot 2).

**`cTextFile` read path — native, with RSA4096 decryption.** Reverse-engineered
the real `cTextFile` methods in libmvos (`OpenR` `0x64f70`, `ReadLine` `0x65070`,
`CountLines` `0x65110`): `OpenR` checks the 7-byte `ID` (`RSA4096`) then
`SeekB(0)`; `ReadChar` applies the XOR by body position; `ReadLine` reads to
`\n`/EOF/`max-1` stripping `\r`; `CountLines` counts `\n`. Object layout: `+0x08`
open-flag (0=open), `+0x0c` fs, `+0x14` filename, `+0x20` encrypted marker.
Reimplemented all of it natively (`mvos.cpp`) over an in-memory decrypted body
keyed by the guest object address — `OpenR`/`ReadLine`/`CountLines`/`Close`/
`IsOpen`, sharing the exact XOR from `tools/theocracy_crypt.py`. Guest paths
`data/…` resolve under `$THEOC_DATA` (default `data/game`). `Fatal(char*)` now
prints each distinct message once (then continues, for bring-up visibility).

**`sscanf` — from-scratch scanf engine** (`traps.cpp`, `do_sscanf`). Parses the
format over guest memory and stores through the guest vararg pointers: `%d i u
x X o p f e g s c`, scansets `%[...]`/`%[^...]` (with ranges), `%n`, width, `*`
suppression, and `h/hh/l/ll/q/L` length modifiers; returns the assignment count.
This was the **keystone**: it was the #1 unimplemented call (8430) *and* — not
binary `cFile` as first guessed — the reason the `.spn` sprite loader failed. It
parses its data via `sscanf`, so the stub returning 0 made it miss the `-1`
terminator. With real `sscanf` the loader works and **all 169 `Nincs -1` asserts
vanish**.

**Batch of MVOS methods** (from the real libmvos bodies): `cRandom::Rnd` (LCG
`state=state*0xFFFFFFF1+0x7FFFFFFF`, returns `state/4294967295.0`), the remaining
`cData_Sample`/`cData_Palette`/`cData_Font` ctors + improved `cData_Bitmap`/
`AnimBitmap` (unified lazy-`cMemBlock` descriptor: primary vtable `+0x08`, payload
`+0x1c`, with base-vtable fallback), `cNode`/`cHNode::UnLink`, `cList::UnLinkList`,
`cLocaleEntry` (vtable + name `+0x0c`; DB registration deferred).

**x87 float returns.** `cRandom::Rnd` returns a `double` in st0, which an
eax-based trap can't set. Added `Machine::return_double`: it stashes the value at
a scratch address and redirects EIP into a one-time guest stub `FLD qword
[scratch]; RET` — real x87 does the FPU push, and the trap's return address stays
on the stack for the stub's RET. Verified stable across 2685 `Rnd` calls (no FPU
stack corruption). Reusable for any float-returning MVOS method.

## State

```
mvos copyrelocs: 34 vtables (157 slots), 10 singletons wired, 35 other left zero
.ctors done: 215 ok, 0 no-return, 0 faulted     (was 2 faulted in M1, 1 mid-M2)
Init returned  →  all 9 flags still 0→1
implemented imports: 22 (27k calls)  |  UNIMPLEMENTED: 13 imports / 303 calls
Fatal asserts: 0
```

The unimplemented surface is now down to a long tail of low-frequency imports:
`IdentifyFileSystem` (169 — harmless, our native `cTextFile` ignores the fs
object), `cMsgSender`/`cMsgReceiver`, `cDayTime` setters, `cVOConsole`/`cVOAButton`
ctors, `GetPalette`, `cShell`, `cSprABitmapAdd` — a handful of calls each.

The pointer-sprite ctor (`0x817e9d0`) that faulted in M1 now runs clean — its
lazy-load vcall resolves to a vtable trap (`[vtable] TODO __vt_12cData_Bitmap[2]`)
instead of dereferencing a null vtable. With `cTextFile` live, the game now
**reads and parses its real (decrypted) config files** — `sscanf` calls jumped to
8430 as it parses config lines, and execution reaches the **unit-animation
loader**: 169 distinct `Fatal("Nincs -1 : data/mananim/<unit>_<action>.spn")`
asserts ("Nincs -1" = Hungarian "there is no -1") — the `.spn` sprite loader
failing its `-1` terminator check because binary `cFile` reads aren't implemented
yet. That is real forward progress: we're now inside asset loading.

## The last ctor fault — RESOLVED (and the old hypothesis was wrong)

`ctor #213 @0x8064bd0` → `FUN_08063600` (reads `data/rd/rd.txt`, the intro-demo
subtitle table, and builds `cVObject` widgets) → `FUN_0816cc80`
(`cSprite`-from-`cData_AnimBitmap` ctor). A game-side Ghidra pass on
`theocracy.real` traced the source object **`DAT_085bf980`** and debunked the M1
guess (a "libmvos-side system global with no ctor"). It is an **ordinary
`cData_Font("data/fonts/small_1pix.mft")`**, constructed by `FUN_08141f30`
(renamed **`RegisterGlobalResources`**, ctor #194) — a ~200-object resource
registrar that our HLE already runs, in the correct order (its `.ctors` slot
`0x08597454` runs before `#213`'s slot `0x08597408`). `cData_Font` derives from
`cAnimBitmap`, so `GetPalette__11cAnimBitmap(&DAT_085bf980, …)` and the sprite
ctor legitimately consume it. The fault peeled off in three stages:

1. **`eip=0x816cdeb accessing 0x8`** — null payload vtable. Already fixed by the
   prior batch's `make_descriptor` (sets `+0x1c` via the `__vt_10cMemBlock_`
   fallback), so the lazy-load vcall lands in a vtable trap, not a null deref.
2. **`eip=0x816ce13 accessing 0x41c`** — `MOV EAX,[EDI+0x40]` reads the
   `cAnimBitmap` **frame/anim-header pointer**, then `*(hdr+0x41c)` = sprite
   height. Our no-op lazy-load never populates `+0x40`, so it was 0 → deref of
   `0x41c`. **Fixed** with a shared zeroed `NULL_FRAME` page: `make_descriptor`
   points every `cData_*`'s `+0x40` at it, so unloaded sprites read a size of 0
   instead of faulting. The real asset lazy-load will overwrite `+0x40`.
3. **`eip=0x8063a2a accessing 0x0`** — `REPE CMPSB` over a null token: the
   `rd.txt` parse calls **`strtok`, which was unimplemented** (returned 0),
   raising `Fatal("Error in config file line 0!")` (non-fatal in bring-up) and
   then dereferencing the null result. **Fixed** by implementing `strtok`
   natively (`traps.cpp`, in-place tokenizer with a saved guest resume pointer).

Result: **215/215 ctors clean, 0 faults, 0 Fatals**, Init still reached, 9 flags
still 0→1. The two remaining `[vtable] TODO` hits (`__vt_10cMemBlock_[2]`,
`__vt_12cData_Bitmap.10cMemBlock_[2]`) are the lazy-load hooks firing as
no-ops — the exact seam where real asset decode plugs in next.

## M2 work order — updated

Done: vtable synthesis, singletons, **`cTextFile` (+RSA4096)**, **`sscanf`**, and
the **MVOS method batch** (`Rnd` + x87 returns, `cData_*` ctors, `cNode`/`cList`
unlink, `cLocaleEntry`). The boot now runs with only a low-frequency tail left.
Next frontiers (bigger than the tail):
1. ~~The remaining ctor fault~~ **DONE** — see the RESOLVED section above
   (`cData_Font` + `NULL_FRAME` scaffold + native `strtok`). All 215 ctors clean.
2. ~~Video: `cVVC`/`cGD` (first pixels)~~ **STARTED** — SDL backend + native
   `cVVC::OpenDisplay` landed; window opens. See "Video backend" below. Next:
   the `cGD` draw traps + the `cScreen`/`cGD` object graph so the game itself
   drives pixels (blocked on the `Start` wall, below).
3. **Video draw path: `cGD`** — implement the imported draw primitives
   (`cGD::Box`/`Frame`/`FrameAlpha`/`HLine_`/`VLine_`/`Tile`, `PutText`,
   `cScreen::BeginRefresh`/`EndRefresh`) as writes into the RGB565 framebuffer,
   plus a `cScreen`/`cGD` object graph the game can navigate to obtain the `cGD`.
4. **Real `cSystemMemory`** (guest-backed evictable blocks) + **binary
   `cFile`/`.spn` decoding** — needed once assets must actually render.
5. Long tail: `IdentifyFileSystem`, `cMsgSender`/`cMsgReceiver`, `cDayTime`,
   `cVOConsole`/`cVOAButton`/`cShell` ctors — implement as each is reached.

## Video backend — SDL window + native `cVVC::OpenDisplay` (first pixels)

`port/src/video.{hpp,cpp}` is a native SDL2 backend that **replaces the whole
`libmvos_vvc_x.so` X11/MIT-SHM plugin** (per [vvc_x-backend.md](vvc_x-backend.md)
— the plugin only *presented*, so an SDL texture-update + present is the entire
contract). It owns an RGB565 framebuffer, an SDL window, and a streaming
texture; `present()` pushes the framebuffer and pumps events.

The video boundary is **direct import traps, not virtual dispatch** — the game
calls concrete `cVVC`/`cGD` methods as imported symbols (confirmed: no
`__vt_*cVVC*`/`__vt_*cGD*` in the copyrelocs). First handler:
**`cVVC::OpenDisplay(cVModeRequest&)`** (`mvos.cpp`). Request layout confirmed
from libmvos `0x95ce0`: `+0 w, +4 h, +8 depthCode` (matching `cVVC+0x20/+0x24/
+0x1c`). The handler reads the request, opens the SDL window, records w/h/depth
back into the guest `cVVC` object at libmvos's own offsets, and returns success.
Depth code 5 = RGB565 (SP primary). `libmvos.so`'s `OpenDisplay` internally
drives a dlopen'd backend mode-setter + double-buffered `cScreen`/`cGD` setup —
all collapsed into "open a window" here.

**Verified end-to-end** (`THEOC_VIDEO_TEST=1`): read the `VVC` singleton pointer
(`0x08598cec` → `0x51002000`), build an 800×600/depth-5 `cVModeRequest` in
scratch, invoke the `OpenDisplay` trap directly → SDL window opens, a test RGB565
gradient renders (framebuffer→texture→present produces visible pixels). This
proves the backend independently of `Start`.

## `cApplication::Start` — singleton virtual dispatch + progress

`THEOC_START=1` calls `cApplication::Start(argc, argv)` (`0x08144650`). Decompiled
(a top-level game state machine — see the plate comment): single-instance IPC
lock → skip logo/intro movies → 16 `cSoundServerChannel`s → CD thread →
`ActivateScreen` → `MainMenu_Run` → per-selection `OpenDisplay` + game setup.

The blocker was **singleton virtual dispatch**: the game calls polymorphic
methods on the framework singletons (`IPCSystem`, `SoundCard`, `VCD`, …), but
their class vtables are **not** copy-relocated and our singletons were zeroed
objects → first virtual call = null-vtable deref (`Start+0x36`,
`IPCSystem->vtbl[3]`).

**Mechanism landed** (`mvos.cpp`, `wire_singleton_vtables`): a reserved pool of
1024 extra VT-trap slots; every singleton's backing object gets a synthesized
32-slot vtable so virtual calls route to `dispatch_vtable` (log + return 0)
instead of faulting; `hook_vslot()` installs native overrides for specific slots.
Overrides so far: `IPCSystem->vtbl[3]` returns a fake lock (passes the
single-instance check), `VCD+0xc` → a synth driver table (CD thread ctor).

With those, `Start` now runs through the IPC lock, sound-channel setup, the CD
thread ctor, and **all the way into `MainMenu_Run`** — it builds all 11
`cVOAButton`s (Single/Multi/Intro/…). Two sub-walls cleared there:
- The button ctor (`0x816e3d0`) faulted reading the button's **localized text**
  (`*text`, null): `GetText` is *inlined* as a read of the `cLocaleEntry` text
  field (`+0x10`), which `cLocaleDataBase::Load` (unimplemented) never fills.
  **Placeholder:** the `cLocaleEntry` ctor now points `+0x10` at the key string,
  so labels are non-null (showing the key) until real locale loading lands.

Earlier the wall was the render system: `CheckCD_Screen` (`0x81a50d0`) and the
menu refresh read `*(Intuition+0x24)` = the active `cScreen`, null because our
`Intuition` singleton was bare-zeroed and `ActivateScreen` was stubbed. Now
resolved:

### Render loop LIVE — the game drives the SDL window
The three render-boundary imports are reimplemented natively (`mvos.cpp`) from
the libmvos originals:
- **`ActivateScreen__10cIntuition`** (`0x9d830`): the game's `cScreen` header IS
  a `cVModeRequest` (w/h/depth @ +0/+4/+8), root `cVObject` at +0x14. Native
  handler opens the SDL window from that header and stores the screen at
  `Intuition+0x24` (the active screen the render path reads), returns success.
  (Real one also calls `OpenDisplay(VVC, screen)` — collapsed here.)
- **`BeginRefresh__7cScreen`** (`0x9d2a0`): pumps the SDL event queue.
- **`EndRefresh__7cScreen`** (`0x9d2d0`): presents the framebuffer. (Real one
  does `PaintTree(root, *(VVC+0x14))` then `SwapBuffers` — widget paint is TBD.)

**`cApplication::Start` now runs end-to-end with no fault**: boot → `Init` →
`MainMenu_Run` builds all 11 buttons → `ActivateScreen` opens the window → the
`BeginRefresh`/`EndRefresh` loop presents frames, waiting on the event pipe for a
selection (hits the 15s emulation timeout, clean). A placeholder gradient is
drawn on activation so it's visibly the game's own screen. `THEOC_START=1` to
drive it. Remaining unimplemented calls are all harmless no-op stubs
(`VM_GetCDRomName`, `Launch__7cThread`, `Load__15cLocaleDataBase`, `cShell`/
`cSemaphore` ctors, …).

**Next: actual widget content** — a `cGD` bound to `Video::fb()` at `VVC+0x14`, a
`PaintTree` walk in `EndRefresh`, the `cGD` primitives (`Box`/`Frame`/`Tile`/
`PutText`) + widget `Paint` methods (menu buttons/background/text), then input
(SDL → `cKeyboard`/`cMouse`) to select a menu item.
