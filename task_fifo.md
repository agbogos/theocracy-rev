# Task FIFO — Theocracy guest-libmvos

Tracking only. Not a design doc. Remaining items, top = next. Playable
single-player baseline is reached; see `docs/porting/guest-libmvos.md` for what
already landed and `user-test.md` for the manual QA pass.

## Remaining (FIFO — prefer top)

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

2. **Long-session stability** — soak-test a 10+ min session. Suspects: the
   green-run/timer trampoline and the soft-threaded sound mixer (slow ESP drift
   self-heals on EBP epilogues, but unverified over hours).

3. **Real threads / signal delivery** — sound mixer runs as a green-thread slice
   off `present`, not a host thread; no real signal delivery / multi-tick
   catch-up when frames stall. Fine today; revisit if timing gets tight.

4. **Full UI-surface coverage** — main flows verified (menu, realm, units, war,
   save/load). Remaining screens (diplomacy detail, tech, scenario/campaign
   menus, options) surface bugs only by playing — driven by `user-test.md`.

5. **Multiplayer** — sockets stubbed; untouched.

6. **Polish** — movie A/V tail alignment (audio can end a beat before video);
   keyboard `[` `]` absent from the eKey table; abandoned guest
   SwapBuffers/BeforeSwapBuffer path (HLE present used instead).

## Done

- **Province-view "performance" — was wall-clock timing, not throughput** —
  three coupling bugs, none CPU-bound (blit overrides removed real cost but
  didn't move the needle). (1) present-coupled 30Hz heartbeat ran at ~6Hz →
  frame limiter over-slept → 12fps + laggy input; fixed by delivering the tick
  from inside `usleep` (Linux EINTR semantics). (2) frame-tied sim → capped
  render to the designed 12fps (`THEOC_FRAME_MS=83`). (3) fps-coupled audio
  mixer → buffer-driven + serviced from `usleep`. Diagnostics: `THEOC_FPS`,
  `THEOC_AUTO_PROVINCE`, block counter. Native LFB16 blit family also landed.
  Full writeup: `docs/porting/frame-timing.md`. Follow-up = FIFO #1 (decouple).
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

- `THEOC_START_SEC` default 600; `0` = unlimited (covers long intros / real play).
- `THEOC_LOUD_ABORT=1` — trap guest abort()/Fatal with a backtrace + stop (debug).
- Keyboard: letters/digits/arrows/modifiers/F-keys/enter/space/backspace; `[` `]` not in original eKey table.
- Audio can stutter in province view (tied to frame cost — see #1).
- Timer: `redirect_guest` from present (nested `uc_emu_start` crashes Unicorn). Sound preferred when its slice is due.

## Quick run

```sh
cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc
# THEOC_SKIP_MOVIES=1   skip cutscenes
# THEOC_AUTO_MENU=1     auto-click Single Player
# THEOC_START_SEC=N     host wall-clock for entire Start() (default 600; 0=unlimited)
```
