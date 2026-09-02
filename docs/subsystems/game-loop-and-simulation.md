# In-Game Loop & Simulation (game binary)

The realm frame loop and the fixed-timestep simulation. Addresses are in the
game executable (`0x08048000`). Reached from
`OpenRealmScreen` → `InitWorldForPlay` → `RealmGameLoop`.

## Two world objects (don't confuse them)
- `g_GameSession` @ `0x84c9610` — the session controller, created by
  `SetupGame`, ~88 bytes: player list (up to 8), pause flag at `+0x50`, mode.
- `g_World` @ `0x85c0b74` — the map/simulation data (large). Key offsets:
  **units** container `+0x1f398` / count `+0x1f3a0`; **provinces** container
  `+0x1468` / count `+0x1470`; tick duration `+0x1408`, sim timer `+0x1410`; the
  **`cDate` game date @ `+0x83c`** ([calendar.md](calendar.md)); a `cMsgSender`
  @ `+0x5c8`; a `cShell` @ `+0x5d8`. This is *the world state*.

  > **Corrected.** The `cShell` was previously given as `+0x176` — an
  > `int *` offset copied out of a decompile without scaling (`0x176 * 4 =
  > 0x5d8`), i.e. [re-methodology](../reference/re-methodology.md) §2. The
  > instruction is `add eax, 0x5d8` at `0x81a6c8b`.

## `InitWorldForPlay(g_World)` (`0x81f9bb0`)
Runs once before the loop. `printf("Units Count:%d")`, initializes the unit
container (`+0x1f398`), iterates the session's ≤8 players doing per-player init
(`FUN_0815af70`), and inits two more subsystems (`+0x1468` provinces,
`+0x1490`).

## `RealmGameLoop` (`0x81a67a0`) — the frame loop
Sets up the realm-view widget tree on `g_RealmScreen`, and an in-game
**developer `cVOConsole`** backed by a `cShell` (`g_World+0x176`) — see the
dev-console note below.

### Dev console — how it's actually gated
There are two consoles. The full write-up, including the shell-plumbing finding
and the `THEOC_CONSOLE` unlock, is in [dev-console.md](dev-console.md).

- Log/output console `g_LogConsole` (`0x85c0fe0`) — `SetExitKey(0xe, mask 2)`.
  It auto-shows when the game prints to it (`FUN_081f3fb0` = Show-then-Print),
  on log or error output. This is the one that gets a
  `cShell` attached (`ChangeShell`, `g_World+0x5d8` here, province`+0x409dc` on
  the province screen).
- Interactive command console `g_CmdConsole` (`0x85c0f80`) — entered via
  `Edit__10cVOConsole` from the key dispatcher `InGame_HandleKeyCommand`
  (`0x81e1aa0`), case `0x21` = Alt+V, and only if `g_GameSession+0x2c != 0`.
  It is never given a shell, so even when it opens a typed command is
  dropped by the null check in `cConsole::Process`.

`+0x2c` is the multiplayer/battle-mode flag, with no debug meaning: it is set to
1 by `NetGame_InitBattle` (`0x829c630`) and force-cleared to 0 by `SetupGame` in
single-player. So the interactive console is live in multiplayer battles and
disabled in single-player. `Console_ShowAndPrint` (`0x81f3fb0`) still auto-shows
the *log* console (`g_LogConsole`) on output either way.

> **Corrected.** This section previously read the exit key `0xe` as
> **Backspace**, and framed the whole thing as "why Backspace does nothing".
> `eKeyCode` is not a PC scancode: in libmvos's dense `KeyTableConvert` enum
> Backspace is `0x36` and `0x0e` is **C**. Confirmed independently inside
> `cConsoleVO::Key`, which maps `0x36` → `RemovePrevChar` and `0x48` → ENTER.

Then, each frame while running:

1. `FUN_08072c60("frame")` brackets the frame — built-in profiling
   (`08072c60`/`c70`/`c90`).
2. `cScreen::BeginRefresh(g_RealmScreen)` — begin drawing to the back buffer.
3. Mouse and focus: if the Intuition mouse (`Intuition+0x14/+0x18`) moved or
   focus changed → `RefreshFocus` and update the hovered `cVObject`.
4. `if (g_GameSession+0x50 == 0) SimulationUpdate(g_World)` — advance the
   sim *only when not paused*.
5. `RefreshTree(mainView)` — draw the map view.
6. Drain the input event pipe (`g_RealmScreen+0x70..0x7c` ring buffer):
   dispatch events, `delete` type `-1`.
7. `UpdateProvincePaletteEffects(...)` — animate province colors.
8. `cScreen::EndRefresh(g_RealmScreen)` — flip the double buffer.
9. `FUN_081a3a70(DAT_084c9764, 1)` — a CD/music state setter.
   Its whole body is `if (state@+0x18 != arg) { Lock(+0x84); state = arg; if
   (+0x90) arg == 4 ? VCD->vt[0x1c]() : FUN_081a3b80(this); Unlock(); }` — so
   after the first frame sets the state to 1 it is a no-op every frame
   thereafter. It is `cVCDThread::SetMood(g_VCDThread, 1)`, setting the CD music
   mood to *realm*; `DAT_084c9764` is the music manager and
   `VCD->vt[0x1c]` is *stop*. See
   [music-and-redbook.md](music-and-redbook.md).

Rendering and simulation are decoupled: the loop renders every frame and
simulates on a fixed timestep gated by pause.

Nothing in this loop is frame-tied. That was checked step by step, because the
port had been capping the realm screen to 12fps on the assumption that it was.
`SimulationUpdate` self-clocks from `elapsed/tickDuration`, and
`UpdateProvincePaletteEffects` derives its pulsing colours from
`SetBySys__8cDayTime`, which is pure wall-clock. Step 9 is the idempotent
CD-state setter above, and everything else is render, input and focus.

So rendering the realm screen faster is what the original would have done on a
faster machine. The loop the port has to worry about is province
(`cProvince_Do`), not this one.

## `SimulationUpdate(g_World)` (`0x81f97e0`) — the fixed timestep

```
ticks = elapsed(world+0x1410) / tickDuration(world+0x1408)
if (ticks > 10) ticks = 10                 // clamp — anti "spiral of death"
for each unit in world+0x1f398:            // per-unit pre-pass
    if FUN_0812bcb0(unit, ticks):          // unit due this batch?
        for i in 0..world+0x1484: FUN_08129830(building_i, unit)
while (ticks-- && world_flag):
    SimulationStep(g_World)                // ONE deterministic tick  [0x81f94a0]
    cDate_ctor_YMD(tmp, 0, 0, 1)           // a cDate of exactly one day
    cDate_Add(world+0x83c, tmp)            // game date += 1 day  <- ONE TICK = ONE DAY
Send__10cMsgSender(world+0x5c8, 2)         // once per update, NOT per tick
advance cDayTime game clock
FUN_081faba0(g_World)  (profiled)          // post-tick (pathfinding/render-prep?)
FUN_081fa6a0(world)
FUN_081fa4f0(world)                        // runs every frame regardless
```

> **Corrected.** The two calls after each `SimulationStep` were
> written here as "inject input/orders". They are not. `cDate_ctor_YMD(tmp,0,0,1)`
> builds a temporary date of one day and `cDate_Add` adds it to the live game
> date at `world+0x83c`. This loop is **the tick→day conversion site**, and there
> is no order injection in it. See [calendar.md](calendar.md) and
> [simulation-step.md](simulation-step.md), "Determinism & lockstep".

What follows from it:

- A deterministic fixed timestep with bounded catch-up (≤10 ticks per frame).
- One tick advances the calendar by exactly one day. The in-game day and the
  simulation tick are the same unit, and the rate at which days pass is set
  entirely by `tickDuration` (`world+0x1408`).
- Lockstep remains a hypothesis. `Send__10cMsgSender(...,2)` fires once per
  `SimulationUpdate` that ran at least one tick — it sits after the while loop,
  not inside it — so it is not a per-tick sync, and the message count does not
  track the tick count. With the shared seeded RNG the design is *consistent
  with* lockstep, but the command channel lockstep requires has not been found.
- Units, buildings and provinces are iterated by index arrays, matching the
  `ManIndexArray`/`BuildingIndexArray` from `SetupGame`.

## `UpdateProvincePaletteEffects(bitmapBlock)` (`0x81f8ff0`)
Per-frame cosmetic pass. Derives pulsing color components from the `cDayTime`
clock, then for each province (`g_World+0x1468`, count `+0x1470`) reads an
effect type via `FUN_081d6300` (enum `eProvPalEfx`, values 1/2/3 else `Fatal("
Invaild eProvPalEfx enum val")`) and writes the pulsing color into a **locked
`cMemBlock`** (`GetAddress` asserts locked — the memory-model contract in
practice). This is the province highlight/blink effect (selection, alerts).

## Named this pass
Functions: `RealmGameLoop`, `SimulationUpdate`, `SimulationStep` (`0x81f94a0`,
inferred), `InitWorldForPlay`, `UpdateProvincePaletteEffects`. Data: `g_World`
(`0x85c0b74`), `g_GameSession` (renamed from `g_GameWorld`, `0x84c9610`).

## Open threads (next targets)
- **`SimulationStep` (`0x81f94a0`)** — the single deterministic tick; decompile
  to find the unit/AI/economy update. This is the core gameplay logic.
- **Unit model:** `g_World+0x1f398` container + `FUN_082cd2a0`/`FUN_082cc030`
  accessors → the `cMan`/unit struct.
- **Province/building model:** `g_World+0x1468`, `+0x147c`, `+0x1490`.
- **The `cMsgSender` at `+0x5c8`** — decode what message `2` carries. This is
  now the *only* candidate for the command/sync channel, since
  `FUN_081a1fa0`/`FUN_081a2180` turned out to be `cDate_ctor_YMD`/`cDate_Add`.
- **`tickDuration` (`world+0x1408`)** — its value is unread; no absolute xref
  exists (it is written through a register-held `this`). Since one tick is one
  in-game day, this single field sets how fast the calendar runs.
- Confirm lockstep by checking the IPC receive side consuming the tick-sync
  messages.
