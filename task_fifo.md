# Task FIFO — Theocracy guest-libmvos

Tracking only. Not a design doc. Remaining items, top = next. Playable
single-player baseline is reached and the manual QA pass is complete; see
`docs/porting/guest-libmvos.md` for what landed and `user-test.md` for the QA
results. Order below is **playability first, modernisation after**.

## Remaining (FIFO — prefer top)

1. **Multi-hour gameplay stress test** — the 20-cycle soak covers one scripted
   path; a real multi-hour session is a human test. Needs a harness first:
   rate-limited logging (no gigabytes), periodic resource snapshots, and the
   watchdog armed, so a fault hours in is diagnosable from the log alone. Build
   the harness, then the user drives.

2. **Multiplayer** — sockets stubbed; untouched. Not implemented, not tested.

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

## Done

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
- **Manual QA pass complete (2026-07-24)** — `user-test.md` fully exercised:
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

- **Host objects planted in guest space must come from `TrapLayer::guest_alloc`.**
  A hardcoded arena address is a delayed corruption bug, not an unused hole (see
  G14) — and now that `free()` really recycles, it would be reused sooner.
  Dedicated regions outside the arena (`SINGLETON_BASE`, `GUEST_FB_BASE`,
  `STUB_CODE`) are the other safe home.
- **`THEOC_WATCHDOG=secs`** — reports a stall and, decisively, whether the guest
  is still executing (spinning, with the EIP) or wedged host-side (with the last
  trap). First thing to reach for on any "it froze".
- `THEOC_SLOWLOG=ms` (default 250) — report any host-side section that blocks the
  emulation thread longer than the threshold. The watchdog says *that* we are
  stuck host-side; this says *which handler*. The frame-cap sleep is discounted.
- `THEOC_WATCHDOG_SAMPLE=<path>` — on a host-side stall, capture a native stack
  (`sample`) of exactly that moment; aggregate profiles can't isolate one.
- `THEOC_SOAK=cycles` / `THEOC_SOAK_PLAY=sec` — drive load/unload cycles
  unattended (menu → Prophecy → OK → province → map → exit → confirm → menu),
  with a per-cycle resource snapshot. Steps wait on the active `cScreen*`
  (`Intuition+0x24`) changing, not on wall-clock, so a slow load delays the next
  step instead of desyncing every click after it; each step has a deadline and
  fails loudly. Clicks are paced aim/press/release 3 frames apart — at 12fps a
  same-frame press is never observed by the game.
- `THEOC_REPORT_CLICKS=1` — log every click as `x,y` + window size + active
  screen, to lift coordinates for a new driver script.
- `THEOC_AUTO_KEYS=1` — taps SPACE every 6s via the real SDL path (skips
  cutscenes; unattended coverage for the keyboard input path).
- Known, accepted: `SMPEG_new` decodes a whole movie up front (~0.9s for the
  intro) and `SMPEG_delete` frees it (~0.4s), so ~1.35s brackets a cutscene.
  Not worth lazy/threaded decode — once per cutscene, and skippable.
- `THEOC_LEGACY_KEYMB=1` — never fill the cutscene-skip key mailbox (intros
  unskippable); A/B switch for input-path hangs.
- `THEOC_HEAP_TEST=1` — run the guest allocator's self-test standalone and exit.
- `THEOC_START_SEC` default 600; `0` = unlimited (covers long intros / real play).
- `THEOC_LOUD_ABORT=1` — trap guest abort()/Fatal with a backtrace + stop (debug).
- Keyboard: letters/digits/arrows/modifiers/F-keys/enter/space/backspace; `[` `]`
  not in the original eKey table (won't-fix).
- Audio can stutter in province view (tied to frame cost — see #3).
- Timer: `redirect_guest` from present (nested `uc_emu_start` crashes Unicorn).
  Sound preferred when its slice is due.

## Quick run

```sh
cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc
# THEOC_SKIP_MOVIES=1   skip cutscenes
# THEOC_AUTO_MENU=1     auto-click Single Player
# THEOC_START_SEC=N     host wall-clock for entire Start() (default 600; 0=unlimited)
```
