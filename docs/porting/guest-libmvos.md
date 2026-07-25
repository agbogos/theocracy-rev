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

- `cIntuition::PushKeyInput` (`mvos+0x8e670`) drains it into the Intuition ring,
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

## Build / run

```sh
cmake -S port -B port/build && cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc \
  data/cd/linux/theocracy.real data/cd/linux/libmvos.so.0.9
# THEOC_LOUD_ABORT=1   trap guest abort()/Fatal with a backtrace + stop
```
