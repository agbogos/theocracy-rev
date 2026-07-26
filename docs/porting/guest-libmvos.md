# Guest libmvos — architecture pivot

**Status: G1 bring-up achieved (2026-07-22).** Pure-HLE MVOS (`mvos.cpp` / `video.cpp`) is left in tree but **not linked**. The host now maps **both** `theocracy.real` and `libmvos.so.0.9` under Unicorn and HLE-only the OS boundary.

## Why

See the project discussion: guest libmvos front-loads “it runs with real engine code”; pure HLE front-loads a native engine rewrite. We chose guest libmvos.

## Layout

| Region | VA |
|--------|-----|
| game (ET_EXEC) | native `0x08048000+` |
| libmvos (ET_DYN) | **`0x10000000`** (`guestlink::MVOS_BASE`) |
| HLE traps | `0x77000000` (unchanged) |
| heap / stack / scratch | `0x60` / `0x70` / `0x50` (unchanged) |

## Linker (`port/src/guestlink.{hpp,cpp}`)

1. Map both PT_LOAD sets (libmvos text mapped **RWX** so text relocs apply).
2. Build global symbol table (game defs preferred).
3. Apply game’s **79 `R_386_COPY`** from libmvos → game `.bss`, then **rebind** those names so DSO `GLOB_DAT` hits the main copy.
4. Resolve UND:
   - game: **194 → libmvos**, **38 → HLE**
   - mvos: **19 → game** (Init/Start/EH/builtins…), **105 → HLE**
   - unique HLE surface: **~119** (libc / pthread / dl / SMPEG / sockets…)
5. Apply **~17.7k** relocs (`RELATIVE` / `32` / `PC32` / `GLOB_DAT` / `JMP_SLOT`).

## Run sequence

```
libmvos DT_INIT → libmvos .ctors (10) → game .ctors (215) → Init__12cApplication
```

## G1 result (first successful run)

```
COPY relocs applied: 79
game UND -> mvos: 194, -> HLE: 38
mvos UND -> game: 19,  -> HLE: 105
relocs applied: 17700
DT_INIT returned
mvos .ctors: 10 ok
game .ctors: 104 ok, then FAULT
  UC_ERR_WRITE_UNMAPPED at eip=0x10053273 accessing 0x6fdffffc
  (= stack growth past STACK_TOP-STACK_SIZE; Fatal/abort storm)
Init returned  (still reached after partial ctors)
```

## G2 — FS HLE + stack/abort (done)

**1. FS HLE** (`traps.cpp`): `fopen`/`fclose`/`fread`/`fwrite`/`fseek`/`ftell`/`feof`/`fflush`/`_IO_getc`, `open`/`close`/`read`/`write`, `remove`, `__xstat`. Guest paths `data/…` resolve under `$THEOC_DATA` (default `data/game`). `/dev/*` returns stub handles (zeros).

**3. Stack + tame abort**: guest stack **2 MB → 16 MB**; `abort`/`exit`/`_exit` call `Machine::request_stop()` (`uc_emu_stop` + EIP→STOP) so Fatal does not unwind through guest EH into a stack smash. Ctors that abort are counted and **skipped**, not fatal to the host.

### G2 result

```
mvos .ctors:  10 ok
game .ctors: 177 ok, 0 aborted, 1 faulted (of 215)
  fault: ctor #177 @0x818a880  FETCH_UNMAPPED eip=0 accessing 0
  (null call target — separate from FS/abort; remaining 38 ctors not run)
Init returned
all 9 subsystem flags 0 → 1   (clean transition; G1 had pre-set 1s from partial state)
implemented traps: 17 (104k calls)   UNIMPLEMENTED: 1 (gettimeofday ×1)
```

No more Fatal/fopen storm in the log. Remaining wall is **ctor #177 null call** (likely a function pointer / vtable / pthread callback still stubbed to 0), then the other 38 ctors.

## G3 — COPY/reloc order + provider fix (done)

**Root cause of ctor #177 `eip=0`:** two linker bugs:

1. **COPY before mvos reloc** — game received pre-reloc zero vtable slots.
2. **COPY source was the game's own .bss** — `R_386_COPY` symbols are *defined* in the game at the destination; prefer-game symbol lookup made `provider == dst`, so COPY was a no-op and mvos `R_386_32` immediates in ctors pointed at empty game vtables.

**Fix:** relocate mvos fully first (local defs always win for mvos `R_386_32`); COPY from `mvos_defs` only; rebind; re-apply mvos GOT only; then game relocs.

### G3 result

```
mvos .ctors:  10 ok
game .ctors: 215 ok, 0 aborted, 0 faulted   ← full set clean
Init returned
all 9 flags 0 → 1
implemented traps: 18 (~114k calls)
UNIMPLEMENTED: 3 low-freq (see trap report)
```

## G4 — plugins, OpenSubsystems, Start (done)

### Landed
- Libc: `strncmp`, `__strtol_internal`, `__strtod_internal`, `gettimeofday`, `strcat`, sockets/`pipe`/`fcntl`/`sem_*`/`setitimer` stubs
- **Synthetic `dlopen`/`dlsym`**: `QueryDevice` + `Create*Device` → guest device objects
- **Flag/EnvSystem/singleton sync** (game↔mvos COPY split — permanent linker fix still TODO)
- Minimal `data/game/mvos.cfg` + mvos.cfg loader @ `0x94640`
- **`cIntuition` construction** after OpenSubsystems
- **HLE `OpenDisplay`** (patch libmvos entry → SDL RGB565 window)
- Boot sequence: DT_INIT → mvos ctors → cfg → game ctors → Init → OpenSubsystems → Intuition → Start

### G4 result
```
Init ok, OpenSubsystems ok (all 4 plugins + SoundCard/IPC)
[video] window 800x600 depth-code 5
[HLE] OpenDisplay 800x600 depth 5 -> ok
Start FAULTED in cVOBitmap::Paint (cGD* null @ +0x14)
```

## G5 — cGD_LFB16 + SwapBuffers present (done)

HLE `OpenDisplay` now builds a guest **`cGD_LFB16`** (layout from ctor `0x6bb30`):

| Off | Field |
|-----|--------|
| +0 / +4 | w / h |
| +8 | framebuffer → `GUEST_FB_BASE` |
| +0xc | depth code 5 |
| +0x10 | pitch = w×2 |
| +0x14 | `__vt_9cGD_LFB16` @ mvos+`0xa2820` |

Wired onto the VVC device at `+0`, `+4`, and `+0x14` (PaintTree path). **`SwapBuffers`** patched to copy guest LFB → SDL and `present()`.

### G5 result
```
[HLE] cGD_LFB16 @… fb=0x53000000 800x600 pitch=1600 vt=0x100a2820
[HLE] OpenDisplay 800x600 depth 5 -> ok
SinglePalette font [data/fonts/small_black.mft]
Start (menu loop — timed out waiting for input)
implemented traps: 42  (~3.1M calls)   UNIMPLEMENTED: 0
```

**The main menu is running** under guest libmvos (refresh/paint/present loop). No faults.

### G5b — mode-switch pitch tear (fixed)

Movies open **640×480**, then the menu opens **800×600**. `Video::open` used to
`return true` if a window already existed, so SDL stayed at 640×480 (pitch 1280)
while the guest `cGD_LFB16` painted at pitch **1600**. Presenting that buffer with
the wrong stride produces the CRT-streak / multi-tile menu background.

**Fix:** recreate the SDL texture + host FB (and `SDL_SetWindowSize`) whenever
`w`/`h` change; `SwapBuffers` also re-opens if GD dims disagree with the window.

```
[video] window 640x480 …
… movies …
[video] window 800x600 …
[HLE] cGD_LFB16 … 800x600 pitch=1600
```

## G6 — SDL input → mouse/keyboard queues (done)

| Piece | Implementation |
|-------|----------------|
| Mouse/pointer devices | Full `cMouse`/`cPointer` ring buffers (0x100 × 12-byte events) |
| `EVENT_Move` / `EVENT_Buttons` | Host-side queue writes (same layout as libmvos) |
| Keyboard | Key-matrix + ring for a few scancodes (Esc/Enter/Space/arrows) |
| Cursor | Host cursor hidden (game draws its own) |
| Wiring | `Video::set_event_hook` → `TrapLayer::on_sdl_event` during `present`/`pump` |

Verified: `[input] mouse btn mask=…` and key events log during the menu loop.

```sh
DYLD_LIBRARY_PATH=/opt/homebrew/lib THEOC_START_SEC=60 THEOC_VIDEO_HOLD=3 \
  ./port/build/theoc
```

## G6b — input fix + CD check + software cursor (done)

Root causes of “input does nothing / no cursor / insert disc”:

1. **Mouse ring indices swapped** — `+0x08` is **read**, `+0x0c` is **write** (empty when equal). Fixed enqueue.
2. **UI polls Intuition, not only VMouse** — `GetIPointerPos` → `Intuition+0xa0`, `GetIMouseButtons` → `+0xa8`. SDL now writes those every move/click.
3. **CD check** — `VM_GetCDRomName` opens `/mnt/cdrom/cd.key` and expects body `"Theocracy"`. Host remaps `/mnt/cdrom/*` → `data/cd/*` (override with `THEOC_CD`).
4. **Cursor** — host cursor hidden but game sprite never set; **magenta crosshair** drawn on the FB each `SwapBuffers`.

## G7 — MPEG path + SMPEG skip HLE (done)

Yes — 640×480 is the intro/logo mode; the hang was MPEG, not resolution.

**Root cause (round 1):** `SMPEG_new` was unimplemented (returned 0) while `SMPEG_error` also returned 0 (“no error”), so `External_PlayAnim` treated a **NULL mpeg** as success and entered the play path → lockup. Movies also looked under `data/game/movie/` instead of the CD tree.

**Fix (round 1):**
- Path: `movie/*.mpg` and bare `*.mpg` → `$THEOC_CD/movie/…` (default `data/cd/movie/`)
- Full SMPEG HLE: open succeeds if file exists; **status always `SMPEG_STOPPED`** so the play loop exits immediately (skip cutscenes). Set `THEOC_SKIP_MOVIES=1` to succeed even without files.

**Root cause (round 2 — freeze with *and* without skip):** after SMPEG open the path still:
1. constructs **`cDisplay`** (`__8cDisplayPvUsUsUsUcUiUlUl`, from libsmpeg `MPEGextra.cpp`) — was UND→TODO no-op
2. allocates a streaming audio `cMemBlock` sized from **`SMPEG_Info.samplerate`** (`Sample_Size[fmt] * chans * samplerate/5`)

With a zeroed `cDisplay` + `samplerate=0` → `MemBlock.Alloc(0)` → `Fatal` → `abort` ignored → Start re-opens subsystems in a loop (looked like a hard freeze). **Independent of `THEOC_SKIP_MOVIES`.**

**Fix (round 2):**
- HLE `cDisplay` ctor (layout: Address/+0, W/H/Pitch u16, Red/Green/Blue masks, bpp)
- Fill `SMPEG_Info` with realistic audio (stereo 22050) + video 640×480 even when skipping

```
Movie (local):ubi_logo.mpg / logo.mpg / intro.mpg
[smpeg] SMPEG_new OK … (skip playback)
[HLE] cDisplay @… 640x480 pitch=1280 bpp=16
Inint menu buttons begin ...
Inint menu buttons done.
[HLE] OpenDisplay 800x600 …
Start (menu loop) ok
UNIMPLEMENTED: 0
```

## G8 — menu click → Single Player / realm shell (done)

**Root cause of dead menu:** HLE `SwapBuffers` replaced the real one, which calls
`MouseRefresh` → `PushMouseInput` to drain VMouse into **Intuition+0x28**
(8-byte events). `BeginRefresh` → `ProcessInputs` only looks at that ring.
We wrote VMouse + Intuition pointer fields but never the +0x28 pipe → no
`ProcessTree` → MasterVO never got button ids.

**Fix:**
- SDL motion/button → push Intuition events (type 1 move, type 4 button edges)
- Mouse/pointer devices get a noop function table at `+0x20` (GameSession_LoadSettings
  calls `VMouse[+0x20][+0x18]`)
- `THEOC_AUTO_MENU=1` synthesizes a click on Single Player (`menu.cfg` 20,250)

```
AUTO_MENU aim/L-down/L-up
Single Player
Loading Game / GameSetings Using Default / Loading OK
Shell Changed to Realm Shell
Setup VOConsole …
play.mft / small_black.mft
Start (timed out in realm UI)
```

## G9 — realm playable (partial / user-verified)

Interactive play works end-to-end under guest-libmvos + HLE. User-verified:

- Start a game from the main menu
- Interact with units (select / move)
- Units animate and move on the map
- Diplomacy actions (e.g. declare war)

So the core loop — menu → setup → realm shell → input → sim response → present —
is **running**. G9 is not “done” as polish, but **playability is established**.

### G9b — save-name popup crash (fixed)

Save dialog creates a `cVOEditRow`; `Process` probes shift via
`(*([VKeyboard+0x84]+0x10))(VKeyboard, 0x37/0x38)`. Synthetic keyboard left
`+0x84` null → fault `accessing 0x10`. Also ensure `save/` dirs for writes.

**Fix:** keyboard shell gets a driver function table at `+0x84` with
`Plugin_KeyMatrix` (reads matrix @ `+0x0c`); `fopen` mkdir-p for write modes.

## G10 — polish: audio + MPEG + cursor (partial)

| Item | Status |
|------|--------|
| **Audio** | SDL 22050 Hz stereo; `/dev/dsp` open/write mixes into host queue |
| **MPEG video** | libav → RGB565; paced to stream fps in `SMPEG_playvideoframe` (was turbo free-run) |
| **MPEG audio** | libav audio stream → `swresample` to 22050/S16/stereo, stored per movie; `SMPEG_playvideoframe` pushes one frame's samples (2-frame lead) into the same mixer queue, so cutscene sound tracks the fps-paced video |
| **Cursor** | Guest `cSprite` via real `SwapBuffers__Fv` Before/AfterSwapBuffer; HLE present only |

```sh
THEOC_SKIP_MOVIES=1 ./port/build/theoc   # fast boot
./port/build/theoc                       # real intros
```

### Remaining debt
- Long-session stability, multiplayer
- Province-view performance (also on Win VM)
- *(R_386_COPY shared storage — fixed in G12)*
- *(Fatal/abort policy — loud mode added in G13; default stays non-fatal)*

## G11 — keyboard eKeyCode + Intuition pipe (done)

Earlier G6 used a few **PC scancodes** (Enter=`0x1c`, Space=`0x39`, arrows=`0x48`…).
libmvos `eKeyCode` is a **custom dense enum** from `KeyTableConvert` in
`libmvos_keyboard_x` (XKeysym table): Esc=`0x01`, `1`–`0`=`0x02`–`0x0b`,
`a`–`z`=`0x0c`–`0x25`, arrows=`0x32`–`0x35`, ShiftL/R=`0x37`/`0x38`,
Return=`0x48`, Space=`0x51`, etc.

Also: UI only sees keys via **Intuition+0x28** event types **8 (down) / 0x10 (up)**
→ `ProcessInputs` → `ProcessTree` (`cVOEditRow` handles type 8). Writing the
matrix alone is not enough for text fields.

**Fix:** full SDL scancode → eKey map; push type 8/0x10; matrices + qualifier
byte (`Intuition+0xb0`); VKeyboard ring write idx at `+0x7c`; focus-lost clears
matrices (ReleaseAll).

## G12 — R_386_COPY shared storage (done)

**Problem.** `R_386_COPY` gives the executable (game) ownership of the storage
for globals defined in the DSO (libmvos): VVC/Intuition/… singletons, the 24-byte
`EnvSystem`, the 9 `_12cApplication.*` subsystem flags (45 non-vtable symbols in
all). Real `ld.so` then makes **every** reference in **both** images resolve to
the game's `.bss` copy. Our linker only rebound the GOT (`GLOB_DAT`), but libmvos
reaches these via **`R_386_32`** to its *own* DSO-local slot — so libmvos wrote
its slot while the game read its copy, and they diverged. The workaround was
three manual mvos↔game syncs in `main.cpp` (after mvos ctors, after the cfg
loader, after `OpenSubsystems`), plus a flags mirror — fragile, one-shot, and a
maintained address list.

**Fix (`guestlink.cpp`).** For a non-vtable COPY'd global, resolve libmvos's own
absolute (`R_386_32`) refs to the **game copy** too (`copy_to_game` map consulted
in `build_idx`), so storage is genuinely shared — exactly what `ld.so` does.
Vtables (`__vt_*`) stay pointing at libmvos's own relocated body (virtual
dispatch must hit libmvos code; the game copy is a byte-identical snapshot).
Deleted all three manual syncs + the flags mirror (~50 lines).

```
COPY data globals shared to game storage: 45
… OpenSubsystems returned
VVC (game) = 0x601bc3e0   VKeyboard = 0x601ba230   …   (all populated, no sync)
Single Player → Realm Shell        0 unimplemented, 0 faults
```

## G13 — loud abort mode (done)

Guest `Fatal()` ends in `abort()` (an HLE'd libc import). Default bring-up policy
keeps it **non-fatal** — log and return — so the caller can continue past
non-critical Fatals; the happy path is abort-free so this never fires in normal
play. The risk was that a *real* fault could hide as a silent continue / restart.

**`THEOC_LOUD_ABORT=1`** turns `abort` into a diagnostic: walk the g++ 2.95 EBP
frame chain (`[ebp]`=saved ebp, `[ebp+4]`=ret) and print a labeled backtrace,
then `Machine::request_stop()` the current call so the failure surfaces here.
Addresses are tagged `game 0x080…` or `mvos+0x…` so each drops straight into the
matching Ghidra DB (mvos offset = the libmvos file address).

```
=== [abort] LOUD: guest abort()/Fatal — backtrace ===
  called from mvos+0x951d2
  #0  mvos+0x548f1
  #1  mvos+0x54f8b
  #2  game 0x0814bae8
  #3  game 0x0825721d      <- ctor #104 @0x8257190
  #4  0xdead0000           <- call() STOP sentinel
```

## G14 — the unreserved `cIntuition` (Load Game crash) (done)

**Symptom.** From a cold boot, main menu → **Load Game** faulted; but menu → start
a campaign → quit → Load Game worked. Reported as a load-path bug.

```
Start FAULTED: UC_ERR_READ_UNMAPPED at eip=0x1008d84a accessing 0xc4c4c5e8
```

**Reading the fault.** `eip` − `MVOS_BASE` = `mvos+0x8d84a` =
`cIntuition::ActivateScreen+0x1a`:

```
8d843: mov eax, [edx+0x24]   ; edx = this; +0x24 = currently-active cScreen*
8d846: test eax,eax
8d848: je  +0x3d             ; null → nothing to tear down
8d84a: mov ebx, [eax+0x24]   ; ← fault
```

Unwinding the prologue (`push ebp; sub 0x24; push edi/esi/ebx` → esp0 = ESP+0x34)
gives `this` = `0x60f00000`, return address `0x08146058` in the game's
screen-activation helper (`SetPalette` → `SetPointer` → `ActivateScreen`;
`Intuition` global `0x08598454`, screen global `0x084c9128`).

So `Intuition+0x24` held **`0xc4c4c5c4`** — non-null, so the guard at `8d848`
passed, then the deref faulted. That value is not a stale pointer (the heap is
`0x60xxxxxx`); it is **two adjacent RGB565 pixels of near-identical colour**. The
singleton had been painted over with bitmap data.

**Root cause — a host object squatting in the guest allocator's arena.**
`main.cpp` planted the `cIntuition` object (0xb4 bytes) at a hardcoded address:

```c
uint32_t obj = HEAP_BASE + 0x00f00000;  // "carve from high heap"
```

`HEAP_BASE` is the **bump arena** `bump_alloc` hands out from, starting at
`HEAP_BASE` and growing up, with `free()` a no-op. `0x60f00000` is 15 MB in and
was never reserved — so as soon as cumulative allocation reached 15 MB, the guest
got that memory back as ordinary heap and wrote over the live singleton.

Measured with `THEOC_FPS=1` (which now reports heap + growth rate) driving
`THEOC_AUTO_PROVINCE=1` all the way into province view:

| Phase | Guest heap |
|-------|-----------|
| Main menu (idle) | **3.3 MB** |
| Entering a game | **3.3 → 41.1 MB** in ~1 s (+23.5 MB/s) |
| Province load settled | **50.1 MB** |
| ~20 s of province play | 50.1 MB, **+0.00 MB/s** |

So the 15 MB line is crossed **during game entry**, not at boot: the singleton
survived the whole menu intact and died the moment a game started. That matches
the repro — the crash needs a route that has entered a game at least once. The
remaining route-dependence is just *what* landed there: the fault needs a
previously-active screen (`eax != 0`) **and** garbage that lands unmapped; other
values read junk silently or skip the branch.

Two side observations from the same run: province play is flat at +0.00 MB/s, so
the no-op `free()` does not leak while resident (load/unload cycles are where it
would show); and a corrupted `cIntuition` also holds pointer pos (`+0xa0`),
buttons (`+0xa8`) and pointer-sprite state, which makes it a **likely cause of the
cursor ghost-trail artifacts** reported on post-game screens (Credits, Load Game)
— unconfirmed, pending retest.

**Fix.** Reserve it from the same allocator the guest uses —
`TrapLayer::guest_alloc(0xb4)` (public wrapper over `bump_alloc`). The trap report
now also prints the guest heap high-water, since this bug was completely
invisible before.

```
  Intuition = 0x601c2ca0          (was 0x60f00000)
  guest heap used: 41.3 MB of 128 MB
```

> **Rule.** Host-side objects planted in guest space must come from
> `guest_alloc`, or live in a dedicated region *outside* the arena
> (`SINGLETON_BASE`, `GUEST_FB_BASE`, `STUB_CODE`, `NULL_FRAME`). A fixed address
> inside the arena is a delayed-corruption bug. This was the only such carve-out.

## G15 — real guest allocator (`free()` was a no-op) (done)

**Symptom.** Start a Chronicle → quit → start a new campaign → crash during the
campaign intro. The log says it plainly before the fault:

```
[heap] OUT OF MEMORY requesting 49839 bytes
Start FAULTED: UC_ERR_WRITE_UNMAPPED at eip=0x82c9914 accessing 0
  guest heap used: 128.0 MB of 128 MB
```

The fault site is `mov [eax+edx*4], ecx` with `eax` = 0 — an array append whose
base pointer came from a `malloc` that returned 0. Pure OOM cascade, not a
pointer bug: the arena was simply full.

**Root cause.** The guest heap was a pure bump allocator and **`free()` was a
no-op** (`// bump: no-op`). Every scenario load allocated tens of MB and returned
none of it, so the 128 MB arena was consumed by loading two scenarios in one
session. The engine *does* free correctly — libmvos has whole heap layers
(`cHeap_Compatibility::Free`, `FreeHeapBlock`, `cSystemMemory::Free`) and the
province log is full of `Free Province Bitmap:(…)OK` — all of which bottom out in
the imported `free` we were discarding.

**Fix.** A real allocator in `traps.cpp`: a bump frontier for fresh memory plus a
**coalescing free list**, kept twice — by address (to merge adjacent blocks) and
by size (so allocation is a best-fit `O(log n)` lookup rather than a linear scan,
which matters because a scenario load churns a lot of blocks). `realloc` now
frees the old block after copying; `__builtin_vec_delete` frees too. Unknown
pointers (interior, double-free, not ours) are ignored rather than corrupting the
list.

| | Menu | Province |
|--|------|----------|
| Before (frontier, leaked) | 3.3 MB | **50.1 MB** |
| After (live / frontier) | 2.6 MB | **28.6 / 28.7 MB** |

Frontier now tracks live to within 0.1 MB (7 free blocks), so fragmentation waste
is negligible and a second load reuses the first one's memory instead of
extending the arena.

**`THEOC_HEAP_TEST=1`** runs the allocator standalone against a randomized
alloc/free workload and exits. It guards the failure that would be *worse* than
the leak — two live blocks overlapping, which corrupts guest memory silently:

```
  [heaptest] round 5: 1508 live, 4.44 MB live, 4.59 MB frontier, 465 free blocks
  [heaptest] all freed: 0 B live, 4.59 MB frontier, 1 free blocks
  [heaptest] PASSED
```

18k ops with no overlap or double-hand-out; freeing everything collapses 465
fragments back to **one** block (coalescing is correct); and re-allocating after a
full free reuses rather than extending the frontier.

Reporting now distinguishes **live** (held right now) from **frontier**
(high-water of fresh memory) in both the trap report and the `THEOC_FPS` line —
with a no-op `free` those were the same number, which is why the leak was
invisible.

## G16 — cutscene skip, and the key-event struct we had wrong (done)

Skipping an intro with a key left the game spinning forever once it reached the
menu: no frame, no trap, ~1.2M guest blocks per half-second at `mvos+0x8e6cc`.

The keyboard driver's "next event" method — `[VKeyboard+0x84][+0x0c](sret, this)`
— returns `{int keycode; int flags}`, not the `{count, key}` we assumed:

| field | meaning |
| --- | --- |
| `keycode` | `0` = queue empty |
| `(char)flags < 0` | press (bit 7) |
| `(char)flags >= 0` | release |
| `flags & 1` | "clear the key matrix" request |

Two guest consumers read it, and they care about different fields:

- `cIntuition::PushKeyInput` (`mvos+0x8e690`) drains it into the Intuition ring,
  and **re-polls while `flags & 1` *before* testing `keycode == 0`**.
- `External_PlayAnim`'s play loop (`mvos+0xa1850`) breaks on
  `keycode == 1 && (char)flags >= 0`, bypassing the Intuition ring — this is the
  intro-skip path.

Our first cut at skip filled the mailbox with `keycode = 1, flags = eKeyCode`,
on *every* key-down anywhere, and cleared only `keycode` on read. Pressing SPACE
(eKey `0x51`, odd) therefore left `flags & 1` set permanently, and the next
`PushKeyInput` — i.e. the first one the menu ran — looped on it forever. The
exit test sat one branch past the loop and was never reached.

Fixed by matching the real contract: `flags = 0` (bit 0 clear), the stub clears
**both** words on read, and the mailbox is only ever non-empty **while a cutscene
is on screen** (`movie_playing_`, set from `SMPEG_status`). Outside a movie it
stays empty, so the normal input path is exactly as before — keys already reach
the game via the Intuition ring and the `cKeyboard` matrix, and feeding them here
too would deliver every keystroke twice.

Deliberate deviation: stock only skips on `keycode == 1` (one specific key). We
report `keycode 1` for any key, so **any key skips a cutscene**.
`THEOC_LEGACY_KEYMB=1` reverts to never posting (cutscenes unskippable).

### Instruments added by this hunt

- **`THEOC_WATCHDOG=secs`** (default 10) — a host thread that reports when
  presents stop, and crucially whether the emulator is *still executing guest
  code*: blocks climbing = the guest is spinning, and the reported EIP is the
  loop; blocks frozen = wedged host-side in the named trap. This is what turned
  "it hangs" into `mvos+0x8e6cc` in one run. Freezes are hard to catch
  interactively, so reach for this first.
- **`THEOC_AUTO_KEYS=1`** — taps SPACE every 6s through the real SDL event path,
  from both present sites (so it also fires during cutscenes). The mouse
  self-drivers never pressed a key, which is why this hang had no unattended
  coverage.

Verified: intros skip on space (menu at 18s vs ~85s), then 11 further space taps
in the menu — 0 stalls, steady 12fps, clean exit, 0 unimplemented.

### Correction: the "2s mode-switch stall" was the instrument, not the port

The first watchdog build reported a >2s host-side stall entering the province.
That was a **false positive of our own making**: once `Start` returns, the window
hold presents through `Video::keep_open_for`, which does not go through the
counter the watchdog reads, so the watchdog saw "no frames, guest not executing"
and reported the process *shutting down* as a stall. A stack captured at the
moment of the report showed `~TrapLayer`. Fixed by disarming the watchdog in
`main` before the wind-down; a full province run now reports **zero** stalls.

Worth remembering: an instrument that measures liveness by a counter must be
switched off wherever that counter legitimately stops advancing.

Measured host-side costs, with the deliberate frame-cap sleep discounted from
`THEOC_SLOWLOG` (else every capped frame reports as an 83ms "slow" section):

| section | cost | when |
| --- | --- | --- |
| `SMPEG_new` | 912 / 350 / 178 ms | start of each cutscene |
| `SMPEG_delete` | 443 / 177 ms | end of each cutscene |
| `video_.open` | 209 ms | first window creation, one-time |

The mode switch is innocent. The cutscene cost is real — `SMPEG_new` decodes the
whole movie into RAM up front (the intro is 1192 frames + 1.1M audio samples), so
~1.35s of frozen screen brackets the big one. **Accepted as-is**: it is once per
cutscene, on screens a keypress already skips. Fixing it would mean lazy or
threaded decode in `mpeg.cpp`, with A/V-sync risk, for no gameplay gain.

## G17 — cursor ghost trails: a double-buffer sprite on a single buffer (done)

On static screens (Credits, Load Game) the pointer smeared its whole path into
the background. G14 reduced it (that was `cIntuition` corruption) but a second
cause survived.

`cSprite` keeps **two** saved-background slots, one per buffer:

```c
BeforeSwapBuffer: SaveBg(this, gd, this+0x24); paint at that rect
AfterSwapBuffer:  swap {+0x24..+0x38} <-> {+0x0c..+0x20};
                  RestoreBg(this, gd, this+0x24)
```

It restores the *other* buffer's background — correct when front and back are
different memory, because each buffer's `SaveBg` is taken while that buffer is
clean.

Our `OpenDisplay` points **every** VVC GD slot at one `cGD_LFB16`. On a single
buffer that invariant breaks: `SaveBg` runs over a buffer that still carries the
previous frame's cursor (not erased until later in the same frame), captures
those pixels into the backup, and re-stamps them every frame thereafter. Screens
that fully repaint hide it; static ones accumulate the whole path.

Single-buffer correct is save → paint → present → restore **the same rect**, so
the buffer is clean before the next `SaveBg`. That is `AfterSwapBuffer` minus the
slot swap, patched in memory at `mvos+0x8b69c` (jump the swap block) plus 9 bytes
of NOP at `mvos+0x8b6ec` (stores that would otherwise write uninitialised regs).
`THEOC_LEGACY_SPRITE=1` reverts.

Verified by screenshot on Credits and Load Game (trail gone, single cursor),
province view unaffected, and a 3-cycle soak with heap/ESP identical to the
pre-fix baseline.

### Render-bug harness

A visual bug needs frames, not logs. Added `THEOC_CLICKS="x,y;x,y"` (drive a
click path), `THEOC_MOUSE_SWEEP=1` (drag the pointer so a failed restore leaves
a track) and `THEOC_SHOT_EVERY=N` + `THEOC_SHOT_DIR` (dump frames via
`Video::save_bmp`). Menu button coordinates come from `data/menu/menu.cfg`
(`credits 20 450` → click ~65,460; the offset from the cfg entry to a hit is
about +45,+10).

## G18 — presentation: fullscreen, and movie aspect-fit (done)

Two display-layer items, both in host code only — no guest patching.

### Fullscreen (`THEOC_FULLSCREEN=1`, `Alt+Enter`)

Borderless fullscreen at the desktop resolution (`SDL_WINDOW_FULLSCREEN_DESKTOP`,
never an exclusive mode switch — the guest paints its own mode and logical-size
scaling does the rest). 4:3 is preserved with **pillarbox** bars. `Alt+Enter`
(**⌥Return** on macOS — SDL maps Option to `KMOD_ALT`) toggles at runtime; not
F11, which the game uses.

Most of it was already in place: `SDL_RenderSetLogicalSize` was being called on
every mode open, which both letterboxes the image and makes SDL hand back mouse
coordinates already mapped into guest space — so there is no coordinate mapping
here, and nothing downstream of the framebuffer changed. Verified by clicking a
known button in all four combinations (windowed/fullscreen × HiDPI on/off) and
confirming the logged `x,y` stays in 800×600 space.

Two subtleties worth keeping:
- **`SDL_SetWindowSize` must not run while fullscreen** on a guest mode switch —
  it fights the fullscreen state instead of rescaling, and that transition is
  where G5b's pitch tear lived.
- **`ALLOW_HIGHDPI` is creation-time only.** `SDL_SetWindowFullscreen` cannot add
  it later, so it is set for *both* modes: a window built windowed-without-it
  would make `Alt+Enter` land in a blurrier fullscreen than the env var gives.
  It is also a straight win (800×600 renders at 1600×1200 windowed, and
  2940×1846 at 3.08× fullscreen on a 14" panel). `THEOC_NO_HIDPI=1` reverts.

The `[video]` line reports px vs pt, scale, filtering mode and bar widths, so a
scaling problem is answerable from the log rather than only on screen.

### Crisp UI, smooth video

Fullscreen was slightly blurry because a fractional scale (3.08× on a 2940×1846
panel) resamples every pixel. The fix is **integer scale + nearest**, which makes
each guest pixel an exact N×N block — the default now, not an option. Nearest
matters even at an exact 2× (windowed on Retina): bilinear samples at ±0.25 of a
texel there and still blends.

But the right answer differs by content, because the guest runs two modes:

| Mode | Fractional fit | Integer | Cost of integer |
|------|----------------|---------|-----------------|
| 800×600 (game UI) | 2461×1846 @3.08× | **2400×1800 @3.00×** | ~5% of image area |
| 640×480 (movies) | 2461×1846 @3.85× | 1920×1440 @3.00× | **39% of image area** |

So the UI is **crisp** (integer + nearest) and cutscenes are **smooth**
(fractional fit + linear): flooring 3.85× would throw away nearly 40% of the
picture, and a cutscene is video that `fit_frame`'s bilinear pass has already
resampled, so pixel-exactness buys it nothing.

`Video::set_crisp()` switches both settings together and is a no-op when already
in the requested state. The movie present path asks for smooth on every frame
(cheap, idempotent); **`SMPEG_delete` restores crisp** — the reliable end-of-movie
hook, since every exit path reaches it, including a keypress skip. Verified by the
transitions in the log: 3.00× crisp → 3.85× smooth per cutscene → 3.00× crisp,
and 800×600 settling at 2400×1800 with 270/23 px bars.

### Movie aspect-fit

The shipped movies are exactly two shapes — **480×360** (4:3: `ubi_logo`, the 9
scenario briefings, the 7 tutorials) and **608×300** (~2.03:1: `intro`, `logo`,
`end` and the rest of the main cutscenes) — while the guest opens a single
**640×480** mode for all of them. `SMPEG_playvideoframe` blitted each frame 1:1
at the **top-left** and cropped, so every movie left a differently-shaped margin
of whatever the previous screen had drawn there.

**The trap that made it look like a clamp bug.** The old code clamped the copy to
`cDisplay`'s W/H — but the game constructs `cDisplay` with the **movie's**
dimensions (`608x300`, `480x360`) while setting its **pitch** to the *mode's*
(`1280` = 640×2). So `dw`/`dh` describe the *source*, not the destination, and
`copy_w > dw` could never fire. The real destination extent is the video mode,
and it is only safe to assume that when the target address is the presented
framebuffer (`GUEST_FB_BASE`) — for any other `Address` the surface size is
unknown, so the code degrades to the old 1:1 copy rather than risking an overrun.

**Fix.** Scale to fill the destination on its tighter axis, centre it, and black
the bars — bilinear, RGB565 in and out, in `MpegMovie::fit_frame`. Geometry and
the bar clear are cached and recomputed only when the destination changes, so the
per-frame cost is just the scale. Hand-rolled rather than swscale: the frames are
already RGB565 so there is nothing for swscale's format machinery to do, and a
cached `SwsContext` would add a lifetime to manage across the movie map.

```
[mpeg] fit 480x360 -> 640x480 at +0,+0  in 640x480 (exact fit 0 px)
[mpeg] fit 608x300 -> 640x316 at +0,+82 in 640x480 (letterbox 82 px)
```

Verified by measuring captured frames, not by eye alone: the 4:3 movie has **no**
fully-black rows (it fills 640×480 exactly — both are 4:3), and the widescreen
one has black rows `0–81` and `398–479`, i.e. 82 / 316 / 82, symmetric. No
`[slow]` sections, cutscene audio unchanged, 0 unimplemented.

**Harness gap closed.** Frame capture (`THEOC_SHOT_EVERY`) only ran from the
normal frame path, but cutscenes present from `SMPEG_playvideoframe` — so the
render harness was blind over exactly the frames a video bug appears in. Split
`shot_tick()` out of `render_probe_tick()` and drive it from both. Capture only:
the click/sweep drivers must *not* run during a cutscene, where a synthesized
click would skip the thing being photographed.

## G19 — real BSD sockets (multiplayer transport) (done)

Sockets were unconditional lies (`socket`→32, `bind`→0, `recv`→0) — enough for the
single-instance lock and nothing else. Now real, with the guest↔host translation
the era gap requires. Guest fds share the existing file fd table; the guest tells
us which is which by calling `send`/`recv` vs `read`/`write`, so `close` already
worked.

### Four Linux→BSD divergences, each of which fails silently

The guest is a 1999 Linux i386 binary; we are BSD. Every one of these produces a
plausible wrong answer rather than an error, which is why they are translated
explicitly rather than passed through:

| # | Divergence | Consequence if ignored |
|---|---|---|
| 1 | `sockaddr_in`: Linux `u16 family @0`; BSD `u8 sin_len @0`, `u8 family @1` | host reads family as `len=2, family=0` |
| 2 | `O_NONBLOCK`: Linux `0x800`, BSD `0x0004` | `fcntl` sets `O_ASYNC`+junk, socket stays **blocking** → first `recv` wedges the single-threaded emulator |
| 3 | `errno`: Linux `EAGAIN=11`/`EINPROGRESS=115`/`EADDRINUSE=98` vs BSD `35`/`36`/`48` | a non-blocking socket returns EAGAIN constantly; the guest reads it as a hard error |
| 4 | **`sin_family` is never set by the engine** — it binds `{family=0, port=5043, addr=INADDR_ANY}` | Linux tolerates `AF_UNSPEC` on an AF_INET socket; BSD returns `EAFNOSUPPORT` |

\#4 was not predicted — it was found by dumping the raw guest sockaddr after a
translation failure. Worth the general lesson: **never fail a sockaddr silently**,
because it surfaces as an unexplained `-1` several layers up in guest code. The
first cut did exactly that and cost a debugging cycle.

`select` is real, over the guest's 1024-bit `fd_set` (32-bit words on i386), but
its **timeout is capped at 20 ms**: we are single-threaded, so honouring a long
guest timeout would freeze rendering, input and the audio slice. Capping returns
"nothing ready" early and the guest polls again — exactly what its non-blocking
design already expects. `gethostbyname` is real too (it had to be: `cIPCO_TCPIP`'s
ctor calls it *first* and `perror()`s out if it returns NULL, so a connect could
never even be attempted), synthesising a Linux `struct hostent` in one reusable
guest block.

### The single-instance lock stays faked, deliberately

`cApplication::Start` binds `localhost:5043` and, if taken, `Fatal`s with
`"You can run only one Theocracy in the same time!"`. Honouring that kills the
*second* instance — which is precisely the two-clients-on-one-Mac setup needed to
test multiplayer. So `bind` on port 5043 succeeds without touching the network;
every other port is real. **`THEOC_REAL_LOCK=1`** restores stock behaviour.

That switch also doubles as the proof the transport works end to end:

```
A (THEOC_REAL_LOCK=1, alone):        [net] bind(:5043) ok
B (THEOC_REAL_LOCK=1, while A holds): [net] bind(:5043) failed: Address already in use
                                      You can run only one Theocracy in the same time!
```

Real bind, real `EADDRINUSE`, and the guest correctly interprets the failure —
which exercises the errno translation all the way into guest code. With the
default exemption, two clients both boot to Realm Shell, 0 faults, 0 unimplemented.

Single-player is unaffected: the lock path is the only socket use on that route.

### Follow-up: errno translation is not a socket-call concern

Divergence #3 was first wired into the socket calls only — `socket`, `bind`,
`connect`, `accept`, `send`, `recv`. That turned out to be the wrong boundary.
**`cIPCO_TCPIP` polls its socket with plain `read()`/`write()`, not
`recv()`/`send()`**, so the steady-state path landed in the generic *file* traps
and passed the host errno through untranslated.

The consequence is a good example of a bug that hides in plain sight because the
game still works. libmvos switches on errno values `4..22` and drops anything
outside that window into its generic "unknown error" state, `5`. Linux `EAGAIN`
is `11`, inside the window, and maps to state `3`, which the dedicated server
accepts silently. BSD `EAGAIN` is `35` — off the end of the switch. And every
accepted connection is non-blocking (`cIPCServer_TCPIP::Listen` does
`fcntl(fd, F_SETFL, 0x800)` on it), so **"no packet yet" is the steady state, not
an edge case**: each idle poll of each connected player logged `Error(n): 5`,
thousands a minute, while the game itself played fine. The misreported path was
the idle one.

`to_linux_errno()` already existed with the right mapping; it was only the wiring
that was partial. `read`/`write`/`open`/`remove` now route through it too, and
the bad-fd paths set `EBADF` rather than returning `-1` over whatever errno
happened to be stale.

Generalisable: **a translation layer belongs at the boundary the guest actually
crosses, not at the calls that are named after it.** The guest chooses `read` or
`recv` on a socket for its own reasons.

## G20 — headless dedicated server (`THEOC_SERVER=1`) (done)

The shipped `data/cd/linux/server` (47 KB, stripped, links `libmvos.so` + libc)
now boots under the same host, linker and HLE as the game. Running the original
server binary means **the netgame wire protocol never has to be reimplemented** —
both ends stay original code.

It needed almost nothing new. Its entire external surface is **26 undefined
symbols**, every one already implemented: `main` (from libmvos, same as the game),
`CopyMem`/`Fatal`/`VM_GetCDRomName` from libmvos, and libc/pthread bits we had.
It exports its own `Init__12cApplication` / `Start__12cApplicationiPPc`, exactly
like the game — libmvos owns `main()` either way.

### Headless is derived, not declared

The game copy-relocs all nine `_12cApplication.*` requirement flags; **`server`
carries only `Network`**. So "is this headless" is answerable statically, before
any guest code runs: if the executable has no `_12cApplication.Video` symbol, it
can never ask for a display. That gates plugin/video bring-up and the native blit
overrides, and `Init` then confirms it at runtime:

```
Network    @ 0x0805400c : 0 -> 1   <- set by Init
Sound / Video / Mouse / ... : not in this image
```

This required de-hardcoding the boot path: `main.cpp` had the nine flag addresses
and the singleton globals as literal game addresses (`0x08598xxx`), which are
meaningless in a different executable. They are now resolved **by name** via
`guestlink::abs_sym`, absent symbols simply skipping. Worth noting as a
correctness check: for `theocracy.real` the name lookup reproduces all nine
previously-hardcoded addresses exactly.

### Result — a real listening server

```
[net] socket(type=1) -> guest fd 3
[net] bind(:5042) ok
Theocracy server
[net] accept -> guest fd 4 from 127.0.0.1:61789
```

`lsof` confirms it from outside the emulator — `theoc … TCP *:5042 (LISTEN)` — and
external clients connect and are accepted with correct peer addresses. **The
server's port is 5042, distinct from the game's 5043 single-instance lock**, so
the two do not collide. 0 unimplemented, 2.2 MB guest heap.

### SIGPIPE — a bug the fake sockets were hiding

The first live test exited **141** (`128+13`) the moment a test client dropped:
writing to a socket whose peer has gone raised SIGPIPE and killed the host.

libmvos's `main()` ignores SIGPIPE as its *first* act, precisely because the IPC
layer depends on it — but `signal` was in a bulk stub list returning 0, so the
**host** kept the default disposition. Harmless while sockets were fake; fatal the
moment a real peer disconnected. `signal` now honours `SIGPIPE`/`SIG_IGN` for
real, and sockets additionally carry `SO_NOSIGPIPE` (the BSD equivalent of the
per-send `MSG_NOSIGNAL` Linux code uses) so a write returns `EPIPE` instead.
Verified with three abrupt client disconnects: exit 0, all three accepted.

## G21 — netgame bring-up: server + 2 clients in a lobby (partial)

`[network] enable=1` in `mvos.cfg`, then three emulated processes on one Mac.
**Two clients join a lobby on the real dedicated server and see each other.**

### Reproducing it

> **Correction (2026-07-26).** An earlier version of this section claimed the
> lobby was reached **unattended**. It was not. The click path targeted the
> *"Join server" text* at `505,361`; the actual control is the **small square
> button to its left** (~`466,361`), so the automated run never left the server
> selection screen. Every lobby packet logged below came from the **user clicking
> manually** — joining, entering a player name, then opening lobby settings. The
> transport results (connect/accept) are automation; the lobby results are not.
> Provenance matters here: it also relocates the crash (below) from "somewhere on
> the lobby path" to specifically **map selection**.

```sh
# 1. server
DYLD_LIBRARY_PATH=/opt/homebrew/lib THEOC_SERVER=1 ./port/build/theoc
# 2. each client — Multiplayer, select entry, then the Join *button* (not its label)
DYLD_LIBRARY_PATH=/opt/homebrew/lib THEOC_SKIP_MOVIES=1 \
  THEOC_CLICKS="65,360;350,245;466,361" ./port/build/theoc
```

Two things help drive this:

- **Menu coordinates** come from `data/menu/menu.cfg` (XOR-encrypted; decrypt with
  `tools/theocracy_crypt.py`): `multi 20 350` → click ~`65,360`, per the G17 offset
  rule. The rest were read off a captured frame of the *TCP server selection*
  screen: list entry ~`350,245`, **Join server** ~`505,361`.
- **`data/game/servers.txt`** holds the server list — plaintext, `int32 count` +
  a 40-byte address field + `int32`. It ships pointing at `192.168.0.1`; patching
  the address field to `127.0.0.1` is exactly what the UI's "New entry" would
  write, and avoids driving text entry to test. Original kept as `servers.txt.orig`
  (the file is gitignored extracted data).

### Result

```
CLIENT: gethostbyname('127.0.0.1') -> 127.0.0.1 ; connect(127.0.0.1:5042) ok
SERVER: accept -> guest fd 4 from 127.0.0.1:61832
        accept -> guest fd 5 from 127.0.0.1:61835
c1: Packet(0): Welcome / Create player / MasterPlayer(0) / SetNameAndColor: 0,a
    Packet(1): Create player / SetNameAndColor: 1,b        <- sees c2 join
c2: Packet(1): Welcome / Create player / MasterPlayer(0) / SetNameAndColor: 1,b
    Packet(165): MasterPlayer(1) / Packet(0): DeletePlayer <- master migrates on c1 exit
```

So the lobby has distinct player ids, per-player name/colour propagation, both
peers agreeing on the master, and **master migration plus DeletePlayer** when one
leaves. The engine also logs `No ipx, so going to the TCIPIP section` — the IPX
probe fails and it falls through to TCP/IP, as intended.

Note the server listens on **5042**, while the game's single-instance lock is
**5043** — different ports, so the lock exemption (G19) and the server never
interact.

### Fixed en route: `strrchr`

The first UNIMPLEMENTED trap in a long time — the netgame path is simply the only
route that reaches it. The stub returned 0, guest code called through the NULL,
and it faulted as a fetch at `eip=0`. Implemented `strrchr` **and** `strchr`;
both must return a *guest* pointer into the string, not a host one. Both clients
are back to **0 unimplemented**.

### The map-selection crash — FIXED: `__xstat` overflowed the caller's stack

**Root cause: our `__xstat` wrote 96 bytes into an 88-byte `struct stat`.**

Linux/i386 `struct stat` (`_STAT_VER_LINUX`) is exactly **88 bytes**, and callers
put it on the stack. Our implementation wrote **96** zeroed bytes with an admittedly
guessed layout, so it ran 8 bytes past the caller's local and zeroed the **saved
EBP and return address** sitting immediately after it.

The victim was `cDirent::cDirent(const char*)` (`mvos+0x4c030`), which calls
`__xstat` twice. It completed normally and then `ret`-ed to **0**, popping `EBP` as
**0** — a fault at `eip=0` with no frame pointer, several frames from the real
damage. Only the netgame map dialog constructs a `cDirent`, which is why three
years of single-player never tripped it.

Fixed by writing the real layout, 88 bytes exactly:

```
+0x00 dev(8)  +0x0c ino(4)  +0x10 mode(4)  +0x14 nlink(4) +0x18 uid  +0x1c gid
+0x20 rdev(8) +0x2c size(4) +0x30 blksize  +0x34 blocks   +0x38/40/48 a/m/ctime
```

Two details beyond the size: `S_IFMT` bits agree between Linux and BSD so the mode
passes through unchanged, and passing the **real** mode matters — the old code
hardcoded `S_IFREG`, which would have reported every directory as a regular file
and broken the enumeration even after the overflow was gone. `st_size` also moved
from a guessed `+0x14` to its real `+0x2c`.

### How it was found — three failed inferences, then two instruments

Worth recording, because the reasoning failures were the expensive part:

1. **A stack slot read as a return address.** `0x082bd6e7` was taken from
   `[ESP+0x18]`, six words into the fault dump, and treated as the call site.
2. **An unmeasured GOT value.** "The slot is 0" was read from the **file on disk**,
   never guest memory.
3. **A truncated grep** taken as proof libmvos lacked `__7cDirentPCc`. It exports
   it fine (`0x4c030`).

Each was stated with more confidence than the evidence carried. What actually
solved it was building instruments and reading the result:

- **Zero-GOT scan** (every `JMP_SLOT`/`GLOB_DAT` after linking) — reported **0**,
  killing hypothesis 2 outright. Kept, because a zero slot is otherwise nearly
  undiagnosable.
- **EBP-chain backtrace** on fault — printed "no frame pointer", which was itself
  the clue: at `eip=0` nothing has pushed a frame, so the corruption is *upstream*.
- **`THEOC_TRACE=1` block ring** (last 32 basic blocks, `game`/`mvos+` labelled) —
  showed `mvos+0x4c082 … 0x4c1e8` with trap slots `0x19` and `0x71` in between.
  Decoding `0x4c1e8` revealed it was the **epilogue**, not a call site: the
  function returned to 0. Slot `0x71` decoded as `strrchr` (confirming the slot
  math) and slot `0x19` as **`__xstat`** — called twice, which named the culprit.

The lesson generalises: a fault at `eip=0` with `EBP=0` is a *smashed frame*, not a
null call — look for who wrote past a buffer, not for an unresolved symbol.

## Build / run

```sh
cmake -S port -B port/build && cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc \
  data/cd/linux/theocracy.real data/cd/linux/libmvos.so.0.9
# THEOC_LOUD_ABORT=1   trap guest abort()/Fatal with a backtrace + stop
```
