# Task FIFO — Theocracy guest-libmvos

Tracking only. Not a design doc. Remaining items, top = next. Playable
single-player baseline is reached; see `docs/porting/guest-libmvos.md` for what
already landed and `user-test.md` for the manual QA pass.

## Remaining (FIFO — prefer top)

1. **abort / Fatal policy** — currently non-fatal during bring-up, so a real
   fault can hide as a silent `OpenSubsystems` restart. Add a "loud abort" mode
   (env-gated) that prints the guest EIP/stack and stops, so latent bugs surface
   instead of masking. Useful while chasing #2.

2. **Province-view performance** — the last open functional bug. Slow after
   `setitimer`; also slow on the Win VM, so likely genuine sim/CPU load, not
   timer-gated. Sample the guest hot EIP in province mode: one heavy function
   (sim) vs. death-by-a-thousand-traps vs. host present cost.

3. **Long-session stability** — soak-test a 10+ min session. Suspects: the
   green-run/timer trampoline and the soft-threaded sound mixer (slow ESP drift
   self-heals on EBP epilogues, but unverified over hours).

4. **Real threads / signal delivery** — sound mixer runs as a green-thread slice
   off `present`, not a host thread; no real signal delivery / multi-tick
   catch-up when frames stall. Fine today; revisit if timing gets tight.

5. **Full UI-surface coverage** — main flows verified (menu, realm, units, war,
   save/load). Remaining screens (diplomacy detail, tech, scenario/campaign
   menus, options) surface bugs only by playing — driven by `user-test.md`.

6. **Multiplayer** — sockets stubbed; untouched.

7. **Polish** — movie A/V tail alignment (audio can end a beat before video);
   keyboard `[` `]` absent from the eKey table; abandoned guest
   SwapBuffers/BeforeSwapBuffer path (HLE present used instead).

## Done

- **Auto R_386_COPY shared storage (linker)** — `guestlink.cpp` now redirects
  libmvos's absolute (R_386_32) refs to any non-vtable COPY'd global to the game
  `.bss` copy, so storage is genuinely shared (what real `ld.so` does). Removed
  all three manual `main.cpp` syncs (singletons, EnvSystem, cApplication.* flags,
  ~50 lines). Verified: 45 globals shared, singletons/EnvSystem/flags populate
  themselves, boot→realm clean, 0 unimplemented.

## Notes

- `THEOC_START_SEC` default 600; `0` = unlimited (covers long intros / real play).
- Keyboard: letters/digits/arrows/modifiers/F-keys/enter/space/backspace; `[` `]` not in original eKey table.
- Audio can stutter in province view (tied to frame cost — see #3).
- Timer: `redirect_guest` from present (nested `uc_emu_start` crashes Unicorn). Sound preferred when its slice is due.

## Quick run

```sh
cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc
# THEOC_SKIP_MOVIES=1   skip cutscenes
# THEOC_AUTO_MENU=1     auto-click Single Player
# THEOC_START_SEC=N     host wall-clock for entire Start() (default 600; 0=unlimited)
```
