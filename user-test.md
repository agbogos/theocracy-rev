# User Test — Theocracy (guest-libmvos)

Manual QA pass for you to run when convenient. Goal: exercise the paths I can't
verify headless (actual pixels, actual sound, real mouse/keyboard, "does it feel
right"). Tick each item, jot anything odd in **Notes**, and hand the sheet back —
failures become FIFO items.

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

For a real play session: `THEOC_START_SEC=0 DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc`

---

## 1. Boot & intros (video + the new movie audio)

Run **without** `THEOC_SKIP_MOVIES`.

- [ ] Ubisoft logo plays — **picture** is smooth (not fast-forwarded, not stuttering)
- [ ] Ubisoft logo — **sound** plays and is in sync with the picture
- [ ] Philos/game logo + intro play with picture **and** sound
- [ ] Audio doesn't crackle/underrun during the movies
- [ ] Movie audio ending vs. picture ending feels aligned (a beat of silence at the very end is expected/OK)
- [ ] You can't-easily-tell, but note if any cutscene looks wrong colour (RGB565 tint) or wrong aspect

**Notes:**

## 2. Main menu

- [ ] Menu renders (background art, buttons, text legible)
- [ ] Mouse cursor is visible and tracks smoothly
- [ ] Hovering/clicking buttons gives the expected visual feedback
- [ ] Menu music / ambient sound plays (if the game has it here)
- [ ] Click **Single Player** → advances to game setup

**Notes:**

## 3. Single-player setup → start

- [ ] Setup/scenario screen renders and is navigable
- [ ] Can pick a scenario/campaign and start a game
- [ ] Load progresses to the realm shell without hanging

**Notes:**

## 4. Realm / gameplay

- [ ] Realm map renders correctly
- [ ] Select a unit (click) — selection feedback appears
- [ ] Move a unit — it pathfinds/moves, animation plays
- [ ] Units animate on the map (idle/move) at a reasonable speed
- [ ] Gameplay sound effects fire (clicks, unit actions, ambient)
- [ ] Scroll/pan the map (edge scroll / keys) works

**Notes:**

## 5. Diplomacy / interactions

- [ ] Open a diplomacy screen
- [ ] Declare war (or another diplomacy action) — takes effect, UI updates
- [ ] Any other interaction screens you try render and respond

**Notes:**

## 6. Save / load (+ text input)

- [ ] Open the save dialog
- [ ] Type a save name — **keyboard works in the text field** (letters, digits, backspace, space)
- [ ] Save completes (check a file appears under `save/`)
- [ ] Load the save back — game state restores

**Notes:**

## 7. Keyboard coverage

- [ ] Letters, digits, arrows, Enter, Space, Backspace all register
- [ ] Modifier keys (Shift) behave in text fields
- [ ] Known gap: `[` and `]` aren't in the original eKey table — confirm nothing you need depends on them

**Notes:**

## 8. Known-weak areas (confirm current state, don't expect perfect)

- [ ] **Province view** — open it; expect it to be **slow** (known bug, also slow on the Win VM). Note *how* slow and whether audio stutters there.
- [ ] **Long session** — if you play a while (10+ min), note any slowdown, audio drift, or crash
- [ ] **Multiplayer** — expected non-functional (sockets stubbed); only test if curious

**Notes:**

## 9. Exit / stability

- [ ] Quitting from the menu / in-game exits cleanly (no hang, no crash spew)
- [ ] Over the whole session, note any `FAULTED`, `abort`, or `UNIMPLEMENTED` lines in the terminal

**Notes:**

---

## Summary to report back

- Overall: does it feel playable end-to-end? (yes / mostly / no)
- Top 3 things that felt broken or off:
  1.
  2.
  3.
- Anything in the terminal log worth pasting (faults / unimplemented / abort):
