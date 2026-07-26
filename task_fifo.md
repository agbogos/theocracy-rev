# Task FIFO — Theocracy guest-libmvos

Tracking only. Not a design doc. Remaining items, top = next. Playable
single-player baseline is reached and the manual QA pass is complete; see
`docs/porting/guest-libmvos.md` for what landed. Order below is **playability
first, modernisation after**.

## Remaining (FIFO — prefer top)

1. **Multi-hour gameplay stress test** — the 20-cycle soak covers one scripted
   path; a real multi-hour session is a human test. Needs a harness first:
   rate-limited logging (no gigabytes), periodic resource snapshots, and the
   watchdog armed, so a fault hours in is diagnosable from the log alone. Build
   the harness, then the user drives.

2. **Multiplayer** — transport done (G19); the netgame flow itself is untested.

   **Two-instance test: PASSED (2026-07-26), but for a reason that will not
   survive the work.** Two `theoc` processes ran concurrently on one Mac with no
   contention — both `exit=0`, 0 faults, 0 stalls, 0 unimplemented, both into
   Realm Shell, each with its own SDL audio device; logs differ only by timer
   jitter. **It passes because sockets are fake**: `bind` unconditionally returns
   0, so both instances believe they hold the single-instance lock.
   `cApplication::Start` binds `localhost:5043` and `Fatal`s if taken —
   `"You can run only one Theocracy in the same time!"`, confirmed present in
   `theocracy.real`. So making sockets real **breaks our own test harness**
   unless the lock is deliberately exempted (identify it by port 5043 in `bind`
   and keep it faked / per-instance). Decide that before writing socket code.

   **Topology — use the shipped `server`, do not reimplement it.**
   `data/cd/linux/server` is a **47 KB stripped ELF linking `libmvos.so` + libc**
   — headless (no X11), and tiny next to the 6 MB game. Running it under our
   existing dual-image `guestlink` means **the wire protocol never has to be
   reverse-engineered**: both ends stay original code. `readme.linux` also settles
   the spawn question — in-game server start was broken *on the original Linux
   release* ("a bug in the GNU C library"), with the documented workaround being
   to run `theoserver` manually. So `cTask::Launch` (fork/execlp) is **not** on
   the path; we just launch the server as a third process.

   **Sockets: DONE (G19).** Real BSD sockets with the four Linux→BSD translations
   (`sockaddr_in` layout, `O_NONBLOCK` 0x800→0x0004, Linux errno values, and the
   engine never setting `sin_family` — it binds `{family=0, port=5043,
   INADDR_ANY}`, which Linux tolerates and BSD rejects). `select` is real but its
   timeout is **capped at 20 ms** because we are single-threaded.
   `gethostbyname` is real (the ctor calls it first and gives up on NULL). The
   5043 lock is faked so two clients can coexist; **`THEOC_REAL_LOCK=1`** restores
   stock behaviour and proves the transport (real bind, real `EADDRINUSE`, guest
   correctly Fatals). Two clients both reach Realm Shell, 0 faults, 0 unimplemented.

   **Headless server: DONE (G20).** `THEOC_SERVER=1` boots the shipped
   `data/cd/linux/server` under the same host/linker/HLE — 26 undefined symbols,
   all already implemented. Headless is *derived*: the game copy-relocs all nine
   `_12cApplication.*` flags, `server` carries only `Network`, so no
   `_12cApplication.Video` symbol ⇒ no display bring-up. Boot path is now resolved
   by name (`guestlink::abs_sym`) instead of hardcoded game addresses. It really
   listens — `lsof` shows `TCP *:5042 (LISTEN)` and external clients are accepted.
   **Server port 5042 ≠ the game's 5043 lock**, so no collision. Fixed en route: a
   dropped peer killed the host with SIGPIPE, because libmvos `main`'s
   `signal(SIGPIPE, SIG_IGN)` was hitting a stub.

   **Lobby: WORKING (G21).** `[network] enable=1`, then server + 2 clients as three
   emulated processes on one Mac. Both clients connect to the real dedicated
   server and **see each other**: distinct player ids, name/colour propagation,
   agreement on the master, and master migration + `DeletePlayer` when one leaves.
   Drivable unattended via `THEOC_CLICKS="65,360;350,245;505,361"` (Multiplayer →
   entry → Join server) with `data/game/servers.txt` patched to `127.0.0.1`.
   Fixed en route: `strrchr`/`strchr` (the netgame path is the only route that
   reaches them) — back to 0 unimplemented.

   **Note on provenance:** the lobby packets were produced by the **user clicking
   manually**, not by automation — the scripted path clicked the "Join server"
   *text* rather than the small square button to its left (~`466,361`). Transport
   results (connect/accept) are automated; lobby results were not.

   **Map selection crash: FIXED.** Our `__xstat` wrote **96** bytes into an
   **88**-byte Linux/i386 `struct stat`, running 8 bytes past the caller's stack
   local and zeroing the saved EBP + return address. `cDirent::cDirent`
   (`mvos+0x4c030`, calls `__xstat` twice) therefore `ret`-ed to 0 — a fault at
   `eip=0` several frames from the damage. Only the netgame map dialog builds a
   `cDirent`, so single-player never hit it. Now writes the real 88-byte layout,
   with the **real** `st_mode` (the old code hardcoded `S_IFREG`, which would have
   called every directory a file) and `st_size` at its correct `+0x2c`.
   Found via two new instruments after three failed inferences: a **zero-GOT scan**
   (reported 0, killing the GOT theory) and **`THEOC_TRACE=1`**, a 32-block ring
   dumped on fault — which showed `mvos+0x4c1e8` was an *epilogue*, not a call.
   Rule of thumb learned: `eip=0` with `EBP=0` means a **smashed frame**, not a
   null call.

## Modernisation (deferred — after playability)

3. **Decouple sim from render (frame-tied engine)** — the engine steps
   physics/animation once per rendered frame, and `cProvince::Do`
   (`theocracy.real:0x081da59b`) caps province to its designed **12fps**
   (`0x14585` µs frame limiter). We currently match that (`THEOC_FRAME_MS=83`
   default) for correct sim speed, but 12fps is choppy and — because our
   single-threaded emulator can't run an async heartbeat — the SIGALRM heartbeat
   drops to ~2–8Hz at 12fps (input still fine; it goes through the Intuition pipe
   directly). The proper fix: render at ~30fps but step the sim only every ~2.5
   frames, so it's smooth **and** correct-speed **and** the heartbeat stays 30Hz.
   Needs patching the frame-tied stepping in `theocracy.real` (game-logic
   surgery) — the "gradually rewrite the game natively" territory. See
   `docs/porting/frame-timing.md`.

4. **Real threads / signal delivery** — sound mixer runs as a green-thread slice
   off `present`, not a host thread; no real signal delivery / multi-tick
   catch-up when frames stall. Fine today; revisit if timing gets tight.

5. **Polish** — abandoned guest SwapBuffers/BeforeSwapBuffer path (HLE present
   used instead).

6. **Upscale filtering / "it looks aged"** — the art was authored for a CRT and we
   present integer-scaled nearest, i.e. perfectly hard pixels that never existed on
   the original display. Note there is **no true antialiasing available** (no
   geometry to sample, no higher-res source art), so this is upscale filtering only.
   Assessed at **~1–2 hours**, not really a track item: sharp-bilinear (nearest into
   a 3× render target, then linear to screen — also wins back the ~5% area integer
   scaling costs) plus an optional scanline knob. Full assessment, including the two
   options deliberately rejected: `docs/porting/upscale-filtering.md`.

## Done

- **Fullscreen + movie aspect-fit (G18, 2026-07-26)** — `THEOC_FULLSCREEN=1` opens
  borderless fullscreen at the desktop resolution, 4:3 preserved with pillarbox
  bars; `Alt+Enter` (**⌥Return** on macOS) toggles at runtime. Cheap because
  `SDL_RenderSetLogicalSize` was already in place — it letterboxes *and* makes SDL
  hand back mouse coordinates in guest space, so there is no mapping code.
  Verified by click-testing all four combinations (windowed/fullscreen × HiDPI
  on/off): coordinates stay in 800×600 space. HiDPI is on in **both** modes because
  `ALLOW_HIGHDPI` is creation-time-only, so a windowed-without-it window would make
  the toggle land in a blurrier fullscreen than the env var gives.
  Movies: the two shipped shapes (480×360 4:3, 608×300 widescreen) were blitted 1:1
  top-left, leaving differently-shaped stale margins; now aspect-fitted and centred
  with the bars blacked. The misleading part was that the old clamp read `cDisplay`'s
  W/H, which hold the **movie's** dimensions while its pitch holds the **mode's** —
  so the clamp could never fire. Full writeup: G18 in
  `docs/porting/guest-libmvos.md`.

- **RE-findings audit vs. the repaired DBs (2026-07-26)** — `FixBogusNoReturn.java`
  un-flagged **495** functions in libmvos and **277** in `theocracy.real`; both
  re-analysed, then every address `docs/` cites was re-checked. **5 wrong claims
  found and fixed, 1 open question closed, 0 wrong claims in the port itself.**

  | Doc | Was | Actually |
  |---|---|---|
  | `application-bootstrap`, `macos-hle-emulator` | main file `0x851e0` | **`0x951e0`** (off by the image base) |
  | `vvc_x-backend` | `LoadDevicePlugins` `0xa49a0` | **`0xa4990`** (the address `OpenSubsystems` calls) |
  | `platform-audio-threads` | ctor ends `cThread::Launch(this)` | **`Launch(this + 4)`** — `cThread` is a secondary base at `+4` |
  | `simulation-step` | province sub-object `+0x103a1` | **`+0x40e84`** — an `int *` index misread as a byte offset |
  | `guest-libmvos` (G16) | `PushKeyInput` `mvos+0x8e670` | **`0x8e690`** |

  Closed: `cSystemMemory::Alloc` evicts **oldest-first** and never reads
  `priority` (+0x18) — resolves `memory-and-containers.md` / `open_questions` #17.
  Also spotted: `Alloc` credits each eviction's size to the budget **twice**
  (engine bug, cold path, left alone).

  **Verified clean, no changes needed:** the whole multiplayer/faction surface
  (`GameSession_Construct` 11 slots, `NetGame_InitBattle` `+0x2c=1` /
  `scenarioID=-1`, `NetGame_AssignTeams`, the console gating, and
  `g_LocalFactionTable` having **no write xrefs at all** — which is what the
  "human is always faction 0 in SP" claim rests on); all of `cTribe` (layout,
  the relations-init ladder, `InitTribeRoster`); `cThread`/`cTask` fork+execlp;
  libmvos `main`'s 10-step boot sequence; `cMemBlock`/`cList`/`cString` layouts.

  **Two premises of the audit item itself were wrong** and are worth remembering:
  - The cited evidence of live damage (`0x9e6cc` → "no function at address") was
    a **tool artifact** — the MCP's `get_function_by_address` matches entry
    points only, so any mid-body address reports this (mid-`main` does too).
  - Clearing the flags did **not** re-merge function *boundaries*. Split
    functions persist with a duplicate `FUN_*` at the old truncation point
    (`main` reports a 48-byte body vs its real `0xfc`). Harmless for decompiling
    — the decompiler follows the fall-through — but **reported body extents are
    unreliable**, and it is exactly how `LoadDevicePlugins` got cited on a
    fragment rather than its entry.

  **Provenance convention going forward.** Two failure modes produced every error
  above, so check for both when lifting an address or offset out of a decompile:
  1. **Pointer-arithmetic offsets.** An offset read off a decompiled `TYPE *`
     parameter is scaled by `sizeof(TYPE)`. `param_1 + 0x103a1` on an `int *` is
     byte `+0x40e84`. Offsets via `*(byte *)((int)p + N)` are already bytes.
  2. **Fragment addresses.** Confirm a cited entry has real **callers**
     (`get_xrefs_to`); a fragment shows only `.eh_frame` DATA refs.

  Load-bearing claims here were cross-checked against **disassembly**, which is
  immune to both. Hot paths (boot, render, input, the allocator) additionally
  carry runtime proof and were not re-derived.

- **Cursor ghost trails fixed (G17)** — `cSprite` runs a two-slot (double-buffer)
  background save/restore, but our `OpenDisplay` points every VVC GD slot at one
  `cGD_LFB16`. On a single buffer `SaveBg` captures the previous frame's cursor
  and re-stamps it forever; static screens accumulate the whole pointer path.
  Patched `AfterSwapBuffer` (`mvos+0x8b69c`) to drop the slot swap = the
  single-buffer form (save → paint → present → restore the same rect).
  `THEOC_LEGACY_SPRITE=1` reverts. Verified by screenshot on Credits + Load Game,
  province unaffected, 3-cycle soak unchanged. New render-bug harness:
  `THEOC_CLICKS`, `THEOC_MOUSE_SWEEP`, `THEOC_SHOT_EVERY`/`THEOC_SHOT_DIR`.

- **Long-session soak PASSED (2026-07-25)** — `THEOC_SOAK=20 THEOC_SOAK_PLAY=20`
  drove 20 full load/unload cycles (menu → Prophecy → OK → province → map → exit
  → confirm → menu) in 9.2 min: **0 stalls, 0 faults, 0 unimplemented**, no
  `[slow]` section over 400ms. Cleared three suspects — guest **ESP identical**
  (`0x6ffff3e4`) across all 20 cycles, so the green-thread mixer does not drift;
  stub page flat at 144 B of 64 KB (stubs are per-device, not per-cycle); fds
  flat at 1. Residual, documented and **not** chased: guest heap live grows a
  very linear **+18 KB/cycle** (11.65 → 12.01 MB over 20), i.e. ~7000 cycles to
  exhaust the 128 MB arena; host RSS +0.45 MB/cycle but non-monotonic (reads as
  allocator caching). G15 was 50 MB/cycle and killed the *second* load — that
  class of bug is gone. Attributing 18 KB/cycle would need an allocation-site
  histogram, which is the tool to build if this ever matters.

- **Cutscene skip wedged the menu in an infinite guest loop (G16)** — skipping an
  intro with SPACE left `cIntuition::PushKeyInput` spinning at `mvos+0x8e6cc`
  forever. The driver's next-event struct is `{keycode, flags}`, not
  `{count, key}`; `flags & 1` means "clear key matrix" and is tested *before* the
  `keycode == 0` exit, so the stale odd flags word from eKey `0x51` never let the
  loop end. Fixed by using the real field contract, clearing both words on read,
  and only filling the mailbox while a movie is actually on screen. Found with
  the new `THEOC_WATCHDOG`. See G16 in `docs/porting/guest-libmvos.md`.

- **Guest `free()` was a no-op → OOM crash on the second scenario load (G15)** —
  Chronicle → quit → new campaign died with `[heap] OUT OF MEMORY` and then a
  write through a NULL `malloc` result (`game 0x82c9914`, `mov [eax+edx*4],ecx`
  with `eax`=0). The heap was a pure bump allocator that never reclaimed, so each
  load leaked its whole working set and two loads exhausted the 128 MB arena.
  Replaced with a real allocator: bump frontier + coalescing free list (indexed
  by address for merging and by size for best-fit), `realloc` frees the old
  block, `__builtin_vec_delete` frees. Province went from a 50.1 MB leaked
  frontier to **28.6 MB live / 28.7 MB frontier**. `THEOC_HEAP_TEST=1` soaks the
  allocator standalone (no overlapping blocks; 465 fragments coalesce back to 1).
- **Manual QA pass complete (2026-07-24)** — the whole sheet exercised by hand:
  boot/intros with A/V, menu, single-player setup, realm, units, diplomacy,
  save/load with text entry, keyboard coverage, clean exit. Playable end-to-end.
  Closed by that pass: **full UI-surface coverage** (remaining screens all
  render and respond), **movie A/V tail alignment** (acceptable as shipped), and
  **keyboard `[` `]`** (won't-fix — absent from the original libmvos eKey table,
  nothing in the game depends on them).
- **Load Game from cold boot crashed (G14)** — `cIntuition::ActivateScreen`
  (`mvos+0x8d84a`) faulted reading `[Intuition+0x24]`, the active `cScreen*`,
  which held `0xc4c4c5c4` — non-null, so the null guard passed. That value is two
  adjacent RGB565 pixels: the singleton had been painted over with bitmap data.
  Cause: the host planted `cIntuition` at a hardcoded `HEAP_BASE+0xf00000`
  ("carve from high heap") — an address *inside* the bump arena and never
  reserved, so once cumulative allocation passed 15 MB the guest allocator handed
  it straight back to the game. Measured: heap is **3.3 MB at the menu** but
  bursts to **41 MB entering a game** and settles at **50 MB** in province, so the
  singleton survived the menu and died the moment a game started — matching the
  repro (the crash needs a route that entered a game at least once). Fixed by
  reserving it through `TrapLayer::guest_alloc`; heap + growth rate now reported
  in the trap report and the `THEOC_FPS` line.
- **Province-view "performance" — was wall-clock timing, not throughput** —
  three coupling bugs, none CPU-bound (blit overrides removed real cost but
  didn't move the needle). (1) present-coupled 30Hz heartbeat ran at ~6Hz →
  frame limiter over-slept → 12fps + laggy input; fixed by delivering the tick
  from inside `usleep` (Linux EINTR semantics). (2) frame-tied sim → capped
  render to the designed 12fps (`THEOC_FRAME_MS=83`). (3) fps-coupled audio
  mixer → buffer-driven + serviced from `usleep`. Diagnostics: `THEOC_FPS`,
  `THEOC_AUTO_PROVINCE`, block counter. Native LFB16 blit family also landed.
  Full writeup: `docs/porting/frame-timing.md`. Follow-up = FIFO #3 (decouple).
- **`THEOC_LOUD_ABORT=1` — loud abort mode** — default abort stays non-fatal
  (log + continue) so the happy path is unaffected; loud mode dumps a guest
  backtrace (EBP walk, `game`/`mvos+off` labels for the two Ghidra DBs) and
  `request_stop()`s the current call so a real fault surfaces instead of hiding
  as a silent restart. Happy path is abort-free; verified against forced Fatals.
- **Auto R_386_COPY shared storage (linker)** — `guestlink.cpp` now redirects
  libmvos's absolute (R_386_32) refs to any non-vtable COPY'd global to the game
  `.bss` copy, so storage is genuinely shared (what real `ld.so` does). Removed
  all three manual `main.cpp` syncs (singletons, EnvSystem, cApplication.* flags,
  ~50 lines). Verified: 45 globals shared, singletons/EnvSystem/flags populate
  themselves, boot→realm clean, 0 unimplemented.

## Notes

The `THEOC_*` knobs used to be listed here. They now live in
**`docs/porting/diagnostics.md`** — all 35 of them, with defaults and units taken
from the source, plus a "which instrument for which symptom" routing table. Two
lists is how one goes stale, so this section keeps only what is a *decision*
rather than a mechanism:

- **Host objects planted in guest space must come from `TrapLayer::guest_alloc`**,
  or live in a dedicated region outside the arena (`GUEST_FB_BASE`, `STUB_CODE`,
  `LIBC_DATA`, `SCRATCH`). A hardcoded arena address is a delayed corruption bug,
  not an unused hole (G14) — and now that `free()` really recycles, it would be
  reused sooner. This and the rest of the host's invariants:
  `docs/porting/host-architecture.md`.
- **Accepted, not a bug:** `SMPEG_new` decodes a whole movie up front (~0.9s for
  the intro) and `SMPEG_delete` frees it (~0.4s), so ~1.35s brackets a cutscene.
  Not worth lazy or threaded decode — once per cutscene, on a screen a keypress
  already skips.
- **Won't-fix:** keyboard coverage is letters/digits/arrows/modifiers/F-keys/
  enter/space/backspace. `[` and `]` are absent from the original libmvos eKey
  table, and nothing in the game depends on them.
- **Known:** audio can stutter during the ~1s province-load compute spike (the
  emulator is genuinely busy and rarely yields). Steady state is clean — see
  item #3 and `docs/porting/frame-timing.md`.

Where the rest went: presentation (fullscreen, `Alt+Enter`, crisp-UI/smooth-video,
HiDPI, movie aspect-fit) is G18 in `docs/porting/guest-libmvos.md`; the timer and
sound green-run splices are in `docs/porting/host-architecture.md`.

## Quick run

```sh
cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc
# THEOC_FULLSCREEN=1    borderless fullscreen, 4:3 pillarboxed
# THEOC_SKIP_MOVIES=1   skip cutscenes
# THEOC_AUTO_MENU=1     auto-click Single Player
# THEOC_START_SEC=N     host wall-clock for entire Start() (default 600; 0=unlimited)
```
