# Task FIFO — Theocracy guest-libmvos

Tracking only. Not a design doc. Update as items land.

## Done (playable baseline)

- Dual ELF + HLE OS boundary, Init / OpenSub, plugins
- Video present (mode switch 640→800, pitch, menu/realm paint)
- Menu click → Single Player → realm; units, war, etc.
- Mouse via Intuition pipe; CD remap; save/load
- Movies open (libav decode path); skip via `THEOC_SKIP_MOVIES`; **paced to fps**
- Save-name dialog (cVOEditRow / keyboard +0x84)
- Gameplay/UI audio (`/dev/dsp` → SDL via soft-thread green-run)
- Keyboard (eKeyCode + Intuition type 8/0x10)
- Game cursor + click anim; setitimer → TimerSystem::Proc

## Outstanding (FIFO — prefer top)

1. **Province mode slow** — still slow after setitimer; also seen on Win VM. Heavier than realm map (CPU/sim load, not timer-gated). Investigate guest hot path / host cost when ready.
2. **Movie audio** — cutscenes are video-only today (libav decode paints frames; no MPEG audio → mixer/SDL). Gameplay SFX work; intros/logos stay silent.

### Notes
- `THEOC_START_SEC` default 600; `0` = unlimited (covers long intros).
- Keyboard: letters/digits/arrows/modifiers/F-keys/enter/space/backspace; `[` `]` not in original eKey table.
- Audio can stutter in province view (tied to frame cost).
- Timer: `redirect_guest` from present (nested `uc_emu_start` crashes Unicorn). Sound preferred when its slice is due.
- User 2026-07-22: baseline in order; province unchanged by timer; movie silence confirmed.

## Platform debt (later)

- Threads (sound mixer still soft-thread / green-run)
- Real signal delivery / multi-tick catch-up without present
- R_386_COPY rebind; Fatal/abort policy; long-session stability
- Guest SwapBuffers/BeforeSwapBuffer path (abandoned as fragile)
- Multiplayer / full UI surface

## Quick run

```sh
cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc
# THEOC_SKIP_MOVIES=1   skip cutscenes
# THEOC_AUTO_MENU=1     auto-click Single Player
# THEOC_START_SEC=N     host wall-clock for entire Start() (default 600; 0=unlimited)
```
