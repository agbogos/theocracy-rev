# User Test — Theocracy (guest-libmvos)

**RUN 2026-07-24 — PASSED.** Everything on this sheet is ticked except the
features that are genuinely not implemented (multiplayer). The game is playable
end-to-end: boot → intros with sound → menu → setup → realm → units → diplomacy
→ save/load → exit.

Two findings came out of the pass, both now tracked in `task_fifo.md`:

1. **Load Game from a cold boot crashed** (`ActivateScreen` fault) — root-caused
   to the host planting `cIntuition` at an unreserved address inside the guest
   bump arena, so the game eventually allocated over the live singleton.
   **Fixed** (see G14 in `docs/porting/guest-libmvos.md`).
2. **Cursor leaves ghost trails** on some screens (Credits, Load Game) — render
   artifact only, no gameplay effect. **Fixed** (see G17): `cSprite` runs a
   two-slot double-buffer background restore, and our `OpenDisplay` points every
   VVC GD slot at a single buffer.

## Changed since this pass (2026-07-24) — needs covering in the next one

Landed after the sheet was run, so **untested by this pass**:

- **Fullscreen** — `THEOC_FULLSCREEN=1`, and `Alt+Enter` (**⌥Return**) to toggle at
  runtime. 4:3 is preserved with pillarbox bars; HiDPI is on in both modes. Worth
  checking: the toggle mid-cutscene and mid-game, and that mouse aim still feels
  right scaled up (coordinates are confirmed correct, but *feel* is not).
- **Movie aspect-fit** — cutscenes are now scaled to fit and centred instead of
  landing top-left with stale pixels around them. Item 1's "wrong aspect" check is
  the one to redo: the 608×300 cutscenes should show 82px letterbox bars, the
  480×360 ones should fill the frame.
- **Province view** — no longer the open perf bug this sheet describes (see item 8);
  it was wall-clock coupling, not throughput, and now runs at its designed 12fps.

Original brief: exercise the paths I can't verify headless (actual pixels, actual
sound, real mouse/keyboard, "does it feel right"). Tick each item, jot anything
odd in **Notes**, and hand the sheet back — failures become FIFO items.

## Build & run

```sh
cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc
```

Env toggles:
- `THEOC_SKIP_MOVIES=1` — skip cutscenes (fast boot)
- `THEOC_AUTO_MENU=1` — auto-click Single Player (unattended smoke test)
- `THEOC_START_SEC=N` — host wall-clock budget for the whole session (default 600; `0` = unlimited — use this for a real play session)
- `THEOC_VIDEO_HOLD=N` — seconds to hold the window open after Start returns
- `THEOC_FULLSCREEN=1` — borderless fullscreen, 4:3 pillarboxed (`Alt+Enter` toggles)

For a real play session: `THEOC_START_SEC=0 DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc`

---

## 1. Boot & intros (video + the new movie audio)

Run **without** `THEOC_SKIP_MOVIES`.

- [x] Ubisoft logo plays — **picture** is smooth (not fast-forwarded, not stuttering)
- [x] Ubisoft logo — **sound** plays and is in sync with the picture
- [x] Philos/game logo + intro play with picture **and** sound
- [x] Audio doesn't crackle/underrun during the movies
- [x] Movie audio ending vs. picture ending feels aligned (a beat of silence at the very end is expected/OK)
- [x] You can't-easily-tell, but note if any cutscene looks wrong colour (RGB565 tint) or wrong aspect

**Notes:**

## 2. Main menu

- [x] Menu renders (background art, buttons, text legible)
- [x] Mouse cursor is visible and tracks smoothly
- [x] Hovering/clicking buttons gives the expected visual feedback
- [x] Menu music / ambient sound plays (if the game has it here)
- [x] Click **Single Player** → advances to game setup

**Notes:**

## 3. Single-player setup → start

- [x] Setup/scenario screen renders and is navigable
- [x] Can pick a scenario/campaign and start a game
- [x] Load progresses to the realm shell without hanging

**Notes:**

## 4. Realm / gameplay

- [x] Realm map renders correctly
- [x] Select a unit (click) — selection feedback appears
- [x] Move a unit — it pathfinds/moves, animation plays
- [x] Units animate on the map (idle/move) at a reasonable speed
- [x] Gameplay sound effects fire (clicks, unit actions, ambient)
- [x] Scroll/pan the map (edge scroll / keys) works

**Notes:**

## 5. Diplomacy / interactions

- [x] Open a diplomacy screen
- [x] Declare war (or another diplomacy action) — takes effect, UI updates
- [x] Any other interaction screens you try render and respond

**Notes:**

## 6. Save / load (+ text input)

- [x] Open the save dialog
- [x] Type a save name — **keyboard works in the text field** (letters, digits, backspace, space)
- [x] Save completes (check a file appears under `save/`)
- [x] Load the save back — game state restores

**Notes:**

## 7. Keyboard coverage

- [x] Letters, digits, arrows, Enter, Space, Backspace all register
- [x] Modifier keys (Shift) behave in text fields
- [x] Known gap: `[` and `]` aren't in the original eKey table — confirm nothing you need depends on them

**Notes:**

## 8. Known-weak areas (confirm current state, don't expect perfect)

- [x] **Province view** — *(premise since resolved: this was wall-clock coupling, not throughput. It now runs at the engine's designed 12fps; audio can still stutter there, tied to frame cost.)* Open it and note whether 12fps feels acceptable.
- [x] **Long session** — if you play a while (10+ min), note any slowdown, audio drift, or crash
- [ ] **Multiplayer** — NOT IMPLEMENTED (sockets stubbed); not tested

**Notes:**

## 9. Exit / stability

- [x] Quitting from the menu / in-game exits cleanly (no hang, no crash spew)
- [x] Over the whole session, note any `FAULTED`, `abort`, or `UNIMPLEMENTED` lines in the terminal

**Notes:**

---

## Summary to report back

- Overall: does it feel playable end-to-end? (yes / mostly / no)
- Top 3 things that felt broken or off:
  1.
  2.
  3.
- Anything in the terminal log worth pasting (faults / unimplemented / abort):
