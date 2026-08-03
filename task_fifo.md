# Task FIFO — Theocracy guest-libmvos

Tracking only. Not a design doc. Remaining items, top = next. **Single-player
and multiplayer are both playable and verified end-to-end**, and the manual QA
pass is complete; see `docs/porting/guest-libmvos.md` for what landed.

## Remaining (FIFO — prefer top)

**Playability is closed as of 2026-08-02.** The heap-leak hunt ended with every
controlled activity saturating, and the last broken shortcuts turned out to be
the macOS dead-key composer rather than the game. Everything below is
modernisation, and none of it blocks playing the game.

Item 1 is by far the biggest and needs game-logic surgery; 2 is small.

1. **Decouple sim from render (frame-tied engine)** — the engine steps
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

2. **Real threads / signal delivery** — sound mixer runs as a green-thread slice
   off `present`, not a host thread; no real signal delivery / multi-tick
   catch-up when frames stall. Fine today; revisit if timing gets tight.

## Done

- **Polish / the "abandoned present path" — closed (2026-08-03).** The item read
  "abandoned guest SwapBuffers/BeforeSwapBuffer path (HLE present used instead)",
  inherited unexamined from the pivot commit where it was "abandoned as fragile".
  **The premise was wrong, and had been for a year.** We replace exactly one
  function — `cVVC::SwapBuffers`, the innermost of three links, each with a
  single call site (`cScreen::EndRefresh` → `SwapBuffers__Fv` → us). The guest
  wrapper still owns `PushKeyInput`, `MouseRefresh` → `cSprite::MoveTo`, the
  frame counter and the `Before`/`AfterSwapBuffer` pair. Far from abandoned, it
  is load-bearing: **G17's cursor-trail fix is a patch inside it.** HLE-present-
  only is architecture, not debt. Chain and cVVC slot layout:
  [docs/porting/host-architecture.md](docs/porting/host-architecture.md), "The
  present chain".

  What was actually vestigial were two bring-up-era defensive paths in
  `HLE_SwapBuffers`, now **deleted**: the per-frame re-stamp of the five VVC GD
  slots, and the G5 magenta crosshair (`draw_software_cursor`) drawn when the
  active screen had no pointer sprite. `cVVC::SetBuffers` is the only guest
  writer of those slots and both its callers are functions we replace, so
  `HLE_OpenDisplay` is now the sole writer.

  **The static argument identified them; a measurement closed them.** Each was
  first made a counter in the trap report — the standing lesson being that an
  instrument reading zero has to be shown capable of non-zero first — and a
  session covering every transition (movies, save/load, quit, new game) read
  **0 and 0 over 466 presents**. *Coverage, not duration:* both paths live at
  screen transitions, so a long session of steady play would have proved nothing
  a two-minute one did not.

  Two corrections fell out. `cVVC+0x08` is **not** a GD slot as the host comment
  claimed — `cVVC::SwapBuffers` treats it as an optional memblock-backed overlay
  bitmap; we write `gd` there, which is meaningless but harmless (its only two
  readers are functions we replace), and it was left alone rather than "fixed"
  blind. And both `cVVC::SwapBuffers` and `cVVC::OpenDisplay` have **truncated
  reported bodies** in the Ghidra DB, so `get_xrefs_to` attributed their tails to
  phantom `FUN_*` and the `SetBuffers` call sites appeared to be in neither
  function; the decompiler followed the fall-through and was right, the xref list
  was not. That is the split-function artifact in `docs/reference/re-methodology.md`,
  caught because the two disagreed.

- **Save-file corruption after ~50 saves — fixed (2026-08-02).** Every save
  appended a **byte-identical** copy of a small group to a list at the end of
  each of the **44 province** blocks. Each list stores its length in a **single
  byte** counting 17-byte units; most provinces add 4 units (68 B) per save,
  map23 adds 5. So the counter climbed 4-5 per save and died at 255 — map23
  first, at **51 saves** — after which the byte wraps, the loader reads the wrong
  length, and the rest of the file is misparsed.

  Fixed at the file boundary the host owns, not by patching the game: the write
  site is behind two layers of virtual dispatch and was never pinned, and
  byte-patching logic we do not fully understand is the riskier change.
  `TrapLayer::collapse_save_file` collapses every run to one group when the game
  closes a save it wrote, so the counter is reset to 4 every time and can never
  approach 255. `tools/fix_save.py` is the same algorithm offline;
  `THEOC_FIX_SAVE=<path>` runs the host's own copy without booting;
  `THEOC_NO_SAVE_FIX=1` reverts.

  **Two things make it safe, and both were earned.** It is anchored on the
  *counter byte*, never on the repetition — once a list holds many identical
  groups the periodicity also holds at offsets *inside* a group, and the first
  cut locked onto a shifted phase and corrupted 86 bytes of a 64-group file. And
  it refuses anything it does not fully recognise: every province gets one group
  per save, so an intact file has the same group count in every list; on an
  already-overflowed file the scan finds 125 "lists" with 2-38 groups and all
  three guards fire. Already-overflowed saves are **detected and reported, not
  repaired** — the counter is the only thing that fixes the phase.

  **Verified end-to-end in the game:** a 6-group save was loaded, played and
  saved — the hook produced exactly the 588,678 bytes the offline tool predicted,
  0 runs left, and **the collapsed save then loaded correctly**, which is the
  direction that proves one group is enough. Also verified on three real saves
  plus a synthetic 64-save overflow: lossless, idempotent, and **the C++ and
  Python agree byte-for-byte on all four**.
  Format, the save call chain, and the separate finding that the 72-byte header
  is 54 bytes of uninitialised stack (written to disk with live guest pointers
  in it): [docs/subsystems/save-format.md](docs/subsystems/save-format.md).

- **Upscale filtering — sharp-bilinear + scanlines (2026-08-02).** The
  "it looks aged" item, closed at the ~1–2 h the assessment predicted. The art
  was authored for a CRT; we were presenting integer-scaled nearest, which is the
  *opposite* — perfectly square pixels that never existed on the original
  display. Plain linear was never the answer either (it mushes the UI, which is
  why G18 chose nearest).

  Sharp-bilinear gets both: guest → a 3× intermediate with nearest, then
  intermediate → screen with linear. The first step keeps every guest pixel an
  exact block; the second only softens the boundaries, because it is
  downsampling something already bigger than the output. Two things fell out —
  the final blit now fits **fractionally**, recovering the ~5% of image area the
  integer floor was costing, and scanlines became nearly free and
  resolution-independent (`THEOC_SCANLINES=N`, off by default).
  `THEOC_LEGACY_SCALE=1` reverts; so does a renderer without render-target
  support, automatically and with a log line.

  Verified headlessly against SDL's dummy driver rather than by eye — pass 1
  produces a pixel-exact 3×3 block, pass 2 lands it dead centre with the
  expected linear spread. **Worth reusing:** a render change that could produce
  a black screen is testable without a display if the assertion is about pixel
  *positions* rather than about it looking right.
  Full writeup: [docs/porting/upscale-filtering.md](docs/porting/upscale-filtering.md).

- **Heap-leak hunt — closed (2026-08-02), five trials, every activity
  saturates.** Closes the old Playability #1 and #2 together. The full record,
  with numbers: [docs/porting/heap-growth-trials.md](docs/porting/heap-growth-trials.md).

  Trials 1, 2, 3 and 6 found idling, the window mode and the realm↔province
  cycle to allocate nothing or to settle at ~8 KB/cycle. **Trial 7** put the
  leading candidate — battles — through nine battles in three phases: three
  fought, three auto-resolved, three synthetic with spawned units, with the
  **same save reloaded between the first six**. Live floor after each reload:
  `30.97 39.79 42.03 43.01 43.61 43.64` — the sixth reload of the same file
  costs **30 KB**. It converges. So do the un-reloaded synthetic battles. The
  frontier sat unmoved at 53.39 MB of 128 MB for the last six minutes.

  Battles do have a price, and it is a **plateau, not a slope**: the settling
  point rises from 30.72 MB (province cycling alone) to ~43.6 MB once battles are
  involved. A watched battle costs ~5× an auto-resolved one per frame, so the
  cost is in the battle *view*, not the resolution.

  Two things fell out: guest work peaked at **9.694M blocks/frame at 2.4 fps**
  against 0.21–0.86M in province, which settles the 2026-07-31 slow battle as
  **genuine saturation** rather than a host stall; and the multi-hour harness
  itself is done and proven, which is what closed #2.

  **Not resolved, and recorded as such:** the two long sessions' +7–11 MB/h was
  never reproduced under controlled conditions. Three readings remain consistent
  with the evidence — a warm-up sampled before it flattened, something slower
  than any trial ran for, or a combination one-activity-per-run cannot reproduce,
  which is the method's own blind spot. Closed on the grounds that every
  measurement says a session survives with wide margin. **Reopen on evidence:**
  an arena actually exhausted, `[heap] OUT OF MEMORY`, or a frontier that climbs
  without flattening. The allocation-site histogram was never built and is still
  the only way to attribute a leak if that day comes.

- **Broken Alt+key shortcuts — fixed and confirmed on a run (2026-08-02).**
  Closes the last playability item. No structure was visible because there were
  two unrelated causes, and the bigger one was the **macOS keyboard layout**
  rather than anything in the game:
  - **⌥I, ⌥U and ⌥N never reached the guest.** SDL2 enables text input at window
    creation, which switches on the macOS input method, and ⌥E ⌥I ⌥N ⌥U ⌥`` ` ``
    are **dead keys** — they begin a diacritic, so the OS holds the event and no
    key-down is delivered. ⌥A is not a dead key (just `å`), which is why it kept
    working and named the cause. Fixed with `SDL_StopTextInput()` after window
    creation; we never consume `SDL_TEXTINPUT`, so the input method bought
    nothing and cost five keys. User-verified: ⌥U and the others now respond.
  - **Eight letters have no handler in the game at all — C E G K O R X Z.** They
    fall to the default case and return. Not our bug, nothing to fix.

  The whole dispatcher is now documented — all 28 keys, read off the jump table
  at `0x838a764` and confirmed against the instruction stream — in
  [docs/subsystems/dev-console.md](docs/subsystems/dev-console.md), "The full
  Alt+key table". **Lesson worth keeping:** translating raw scancodes does not
  make a port immune to the host's input method, because the composer sits
  *above* the key event — the keystroke is gone before any of our translation
  runs, and it fails on a set of keys defined by the layout, which is exactly
  why it looked patternless.

  Not decoded, and only worth doing if a key arrives and still misbehaves: what
  event subcodes 0–13 mean. The pipe at `0x8645360` is not the sim's order queue
  and is drained through a virtual dispatch chain.

- **`[health]` growth measured on the live set, not the frontier (2026-07-27)** —
  the frontier is a *high-water mark*. Before G15, when `free()` was a no-op, it
  tracked the live set exactly; once the allocator started reclaiming, it stopped
  moving as soon as freed blocks were reused. Every growth figure in `[health]`
  was derived from it, so on the 2.28 h session **105 of 137 samples read
  `interval +0.000 MB/h` while the live set was climbing 7.2 MB/h**. Growth is now
  signed and taken from `heap_live_` (a *drop* — teardown, save reload — is real
  information the frontier cannot represent); the frontier is reported as a level
  with its own rate and the resulting arena headroom, which is genuinely a
  frontier question. `[health]` also gained a wall-clock stamp so out-of-band
  notes can be aligned to samples. Written up in `docs/porting/diagnostics.md`,
  "Live set vs. frontier"; the general lesson is `frame-timing.md`'s — **when an
  instrument reads exactly zero, confirm it can be non-zero before believing it.**
  New: `tools/plot_health.py` (stdlib-only, emits SVG) fits the slope and flags
  live-set drops, because the trend is not visible by reading samples.

- **`THEOC_LONGRUN` no longer capped at ten minutes (2026-07-27)** — the harness
  armed the watchdog but left `THEOC_START_SEC` at its 600s default, which is
  sized for "boot, look at it, exit". So the multi-hour session harness ended
  every session at 10 minutes, and ended it with `Start (host Start timeout —
  still in game)` — a line that reads like a fault when nothing had gone wrong.
  Found by the first person to actually drive it. `THEOC_LONGRUN` now defaults
  `THEOC_START_SEC=0` the same way it defaults the watchdog, unless set
  explicitly. **Lesson, and the reason this is written down:** a harness that
  configures *some* of the knobs its own purpose depends on is worse than one
  that configures none, because the one it missed presents as a result.

- **Trap-window page maths asserted (2026-07-27)** — closes the last of the three
  2026-07-26 cleanups. `Machine::add_code_traps` (`port/src/machine.cpp`) now
  checks the page-rounded window against the nearest higher trap base
  (`PLUGIN_TRAP_BASE`/`TRAP_BASE`/`VT_TRAP_BASE`, `0x01000000` apart) and against
  address overflow, throwing with both extents instead of mapping over its
  neighbour. Unreachable at today's ~119 HLE symbols — the value is that the
  failure would otherwise be baffling: the overlap maps silently and it is the
  *neighbour's* traps that stop dispatching, far from the import set that grew.
  Landed with the `THEOC_LONGRUN` harness in the same commit; recorded in
  `docs/porting/host-architecture.md`, "Trap-window sizing".

- **Teardown deliberately skips `CloseSubsystems` (2026-07-27)** — closes the old
  FIFO #1/#2. Decided *not* to call it and deleted the unused
  `kMvosCloseSubsystems` constant so it stops implying teardown is handled.
  Nothing it releases outlives the process (SDL devices are host-owned and closed
  by us; the guest heap is one mapping the OS reclaims), it closes devices rather
  than flushing files, and calling it would run seven HLE close paths nothing
  else exercises at the least diagnosable moment. Reasoning and the revisit
  trigger — a platform like Windows where exit does not reclaim as cleanly — are
  in `docs/porting/host-architecture.md`, "Why teardown skips CloseSubsystems".
  Also drops one of the two standing build warnings.

- **Game singleton addresses resolved by name (2026-07-27)** — closes the old
  FIFO #1. `VVC`, `Intuition`, `VMouse` and `VKeyboard` were hardcoded in
  `traps.cpp`; all four are `R_386_COPY` globals, so `main.cpp` now resolves them
  through `guestlink::abs_sym` and passes them to `TrapLayer::set_game_globals`
  (`[link] game singleton globals resolved by name: 4/4`). Verified end to end:
  boot, display, scenario load, mouse and keyboard paths, 0 unimplemented.
  The two addresses `THEOC_CONSOLE`/`THEOC_EDIT` need (`g_LogConsole`,
  `g_GameSession`) **cannot** be resolved — not copy-relocs, not dynamic symbols,
  and the game is `.symtab`-stripped. Accepted deliberately: they serve developer
  features rather than the port, are default-off, and are guarded so a wrong
  address is a no-op. Reasoning recorded in
  `docs/porting/host-architecture.md`; not tracked here, because there is nothing
  to do.

- **Dev console working in single-player — `THEOC_CONSOLE=1` (2026-07-27).**
  **Alt+V** opens, **Alt+C** closes, on **both** the realm and province screens;
  the shells' full command sets respond (`tribe`, `owner`, `jewel`, `date`,
  `save`, `mannaking`, `allcheat`, …). Verified interactively.

  The console was never compiled out — but three separate things were wrong, and
  only the last one was in the game:
  1. **`vsprintf` was an unimplemented host trap.** It is the first call in
     `cConsole::Input`, so every command formatted into a buffer we never wrote.
     The only printf-family symbol libmvos imports that the host lacked.
  2. **Output went to a console nothing shows.** Commands print via
     `Print(shell->+0x44, …)`, which `ChangeShell` points at `g_LogConsole`,
     while the shipped opener opens `g_CmdConsole` — an object with no shell at
     `+0x38` *and* no display role. It can neither execute nor show.
  3. **The realm screen has no opener at any address.**
     `InGame_HandleKeyCommand` is not a global hotkey handler; keys reach the
     focused `cVObject` through its `vtable+0x10`, and only the province view's
     widget routes Alt+key there. `RealmGameLoop`'s event drain dispatches
     nothing at all.

  So the final implementation **does not patch the game**: the host captures
  Alt+V in the SDL hook and calls `Edit__10cVOConsole(g_LogConsole)` at the next
  present, via the same one-redirect-per-present path the timer and sound slices
  use. The earlier branch patch, opcode signature guard and per-present shell
  mirror are all deleted. Write-up: `docs/subsystems/dev-console.md`.

  Doc errors this corrected: exit key `0xe` is **C**, not Backspace (eKey is not
  a PC scancode; the qualifier mask bit 1 is **Alt**); `cShell` is `g_World+0x5d8`,
  not `+0x176` (unscaled `int *` offset). New methodology entries: §10 (libmvos
  vtables are zeros until `.rel.rodata` is applied) and **§11 (run it and read
  the log before reverse-engineering — both root causes here were host-side and
  visible in output we already had)**.

- **Multiplayer — DONE, verified end-to-end by the user (2026-07-26).** A real
  netgame ran successfully: past the lobby, past map selection, into a played
  game. That closes the whole track, and it closes it *without* the wire protocol
  ever being reverse-engineered — the decision to run the shipped
  `data/cd/linux/server` under the same emulator, rather than reimplement it,
  is what made both ends original code. What it took, in order:
  - **Sockets (G19)** — real BSD transport with four Linux→BSD translations, each
    of which fails *silently* rather than loudly: `sockaddr_in` layout,
    `O_NONBLOCK` `0x800`→`0x0004`, Linux errno values, and the engine never
    setting `sin_family` (it binds `{family=0, port=5043, INADDR_ANY}`, which
    Linux tolerates and BSD rejects). `select`'s timeout is capped at 20 ms
    because we are single-threaded. The port-5043 single-instance lock is faked
    so two clients can share a Mac; `THEOC_REAL_LOCK=1` restores stock behaviour
    and doubles as proof the transport works.
  - **Headless server (G20)** — `THEOC_SERVER=1`, 26 undefined symbols, all
    already implemented. Headless is *derived*, not declared: `server` carries no
    `_12cApplication.Video` flag, so no display is ever brought up. Boot resolves
    by name (`guestlink::abs_sym`) instead of hardcoded game addresses — which is
    what the singleton lookups in `main.cpp` now do for the whole host.
  - **Lobby (G21)** — server + 2 clients as three emulated processes on one Mac:
    distinct player ids, name/colour propagation, agreement on the master, and
    master migration + `DeletePlayer` on leave.
  - **Map selection** — our `__xstat` wrote **96** bytes into an **88**-byte
    Linux/i386 `struct stat`, running past the caller's stack local and zeroing
    the saved EBP and return address, so `cDirent::cDirent` `ret`-ed to 0. Only
    the netgame map dialog builds a `cDirent`, which is why single-player never
    saw it. Cost three wrong inferences before two new instruments settled it;
    the method is written up in `docs/reference/re-methodology.md` §7.
  - Full narratives: G19–G21 in `docs/porting/guest-libmvos.md`. The protocol
    itself, decoded for diagnosis rather than reimplementation:
    `docs/subsystems/multiplayer-and-factions.md`.

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
  Full writeup: `docs/porting/frame-timing.md`. Follow-up = **Decouple sim from
  render**, under Modernisation.
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
  **Real threads / signal delivery** under Modernisation, and
  `docs/porting/frame-timing.md`.

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
