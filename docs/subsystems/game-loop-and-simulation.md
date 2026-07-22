# In-Game Loop & Simulation (game binary)

The heart of the game: the realm frame loop and the fixed-timestep simulation. Addresses in the **game executable** (`0x08048000`). Reached from `OpenRealmScreen` → `InitWorldForPlay` → `RealmGameLoop`.

## Two world objects (don't confuse them)
- **`g_GameSession`** @ `0x84c9610` — session/controller (created by `SetupGame`, ~88 bytes): player list (up to 8), **pause flag @ `+0x50`**, mode. This is *who's playing*.
- **`g_World`** @ `0x85c0b74` — the map/simulation data (large). Key offsets: **units** container `+0x1f398` / count `+0x1f3a0`; **provinces** container `+0x1468` / count `+0x1470`; tick duration `+0x1408`, sim timer `+0x1410`; a `cMsgSender` @ `+0x5c8`; a `cShell` @ `+0x176`. This is *the world state*.

## `InitWorldForPlay(g_World)` (`0x81f9bb0`)
Runs once before the loop. `printf("Units Count:%d")`, initializes the unit container (`+0x1f398`), iterates the session's ≤8 players doing per-player init (`FUN_0815af70`), and inits two more subsystems (`+0x1468` provinces, `+0x1490`).

## `RealmGameLoop` (`0x81a67a0`) — the frame loop
Sets up the realm-view widget tree on `g_RealmScreen`, and an in-game **developer `cVOConsole`** backed by a `cShell` (`g_World+0x176`) — see the dev-console note below.

### Dev console — how it's actually gated (why Backspace "does nothing")
Two consoles exist:
- **Log/output console** `DAT_085c0fe0` — `SetExitKey(0xe, mod 2)`. `0xe` = **Backspace is the CLOSE key**, not open (and with a modifier). It **auto-shows when the game prints to it** (`FUN_081f3fb0` = Show-then-Print), e.g. on log/error output. So Backspace only dismisses it once it has popped up on its own.
- **Interactive command console** `g_CmdConsole` (`0x85c0f80`) — entered via `Edit__10cVOConsole` from the key dispatcher `InGame_HandleKeyCommand` (`0x81e1aa0`) **case 0x21**, but **only if `g_GameSession+0x2c != 0`**.

**`+0x2c` is the multiplayer/battle-mode flag** (not a debug flag): it is set to **1 by `NetGame_InitBattle` (`0x829c630`)** and force-cleared to **0 by `SetupGame`** (single-player). So the interactive console is **live in multiplayer battles** and disabled in single-player. `Console_ShowAndPrint` (`0x81f3fb0`) still auto-shows the *log* console (`g_LogConsole`) on output regardless.

Then, each frame while running:
1. **Profiler** `FUN_08072c60("frame")` brackets the frame (built-in profiling: `08072c60`/`c70`/`c90`).
2. `cScreen::BeginRefresh(g_RealmScreen)` — begin drawing to the back buffer.
3. **Mouse/focus:** if the Intuition mouse (`Intuition+0x14/+0x18`) moved or focus changed → `RefreshFocus` + update the hovered `cVObject`.
4. **`if (g_GameSession+0x50 == 0) SimulationUpdate(g_World)`** — advance the sim *only when not paused*.
5. `RefreshTree(mainView)` — draw the map view.
6. **Drain the input event pipe** (`g_RealmScreen+0x70..0x7c` ring buffer): dispatch events, `delete` type `-1`.
7. `UpdateProvincePaletteEffects(...)` — animate province colors.
8. `cScreen::EndRefresh(g_RealmScreen)` — **flip the double buffer**.
9. `FUN_081a3a70(DAT_084c9764, 1)` — advance animations / frame sync.

So: **render every frame; simulate on a fixed timestep gated by pause.** Rendering and simulation are decoupled.

## `SimulationUpdate(g_World)` (`0x81f97e0`) — fixed-timestep, lockstep-ready
The important one.

```
ticks = elapsed(world+0x1410) / tickDuration(world+0x1408)
if (ticks > 10) ticks = 10                 // clamp — anti "spiral of death"
for each unit in world+0x1f398:            // per-unit pre-pass
    if FUN_0812bcb0(unit, ticks):          // unit due this batch?
        for i in 0..world+0x1484: FUN_08129830(building_i, unit)
while (ticks-- && world_flag):
    SimulationStep(g_World)                // ONE deterministic tick  [0x81f94a0]
    inject input/orders (FUN_081a1fa0 / FUN_081a2180)
Send__10cMsgSender(world+0x5c8, 2)         // tick-sync broadcast
advance cDayTime game clock
FUN_081faba0(g_World)  (profiled)          // post-tick (pathfinding/render-prep?)
FUN_081fa6a0(world)
FUN_081fa4f0(world)                        // runs every frame regardless
```

Key inferences:
- **Deterministic fixed-timestep** with bounded catch-up (≤10 ticks/frame).
- **`Send__10cMsgSender(...,2)` per tick** + deterministic stepping ⇒ **lockstep multiplayer** — every peer advances the same ticks in sync. Explains the IPX/TCP-IP IPC layer being core even to single-player (which also runs the sim identically).
- Units and buildings/provinces are iterated by index arrays — matches the `ManIndexArray`/`BuildingIndexArray` from `SetupGame`.

## `UpdateProvincePaletteEffects(bitmapBlock)` (`0x81f8ff0`)
Per-frame cosmetic pass. Derives pulsing color components from the `cDayTime` clock, then for each province (`g_World+0x1468`, count `+0x1470`) reads an effect type via `FUN_081d6300` (enum `eProvPalEfx`, values 1/2/3 else `Fatal(" Invaild eProvPalEfx enum val")`) and writes the pulsing color into a **locked `cMemBlock`** (`GetAddress` asserts locked — the memory-model contract in practice). This is the province highlight/blink effect (selection, alerts).

## Named this pass
Functions: `RealmGameLoop`, `SimulationUpdate`, `SimulationStep` (`0x81f94a0`, inferred), `InitWorldForPlay`, `UpdateProvincePaletteEffects`.
Data: `g_World` (`0x85c0b74`), `g_GameSession` (renamed from `g_GameWorld`, `0x84c9610`).

## Open threads (next targets)
- **`SimulationStep` (`0x81f94a0`)** — the single deterministic tick; decompile to find the unit/AI/economy update. This is the core gameplay logic.
- **Unit model:** `g_World+0x1f398` container + `FUN_082cd2a0`/`FUN_082cc030` accessors → the `cMan`/unit struct.
- **Province/building model:** `g_World+0x1468`, `+0x147c`, `+0x1490`.
- **Order injection** `FUN_081a1fa0`/`FUN_081a2180` and the `cMsgSender@+0x5c8` — the command/sync channel (multiplayer + input).
- Confirm lockstep by checking the IPC receive side consuming the tick-sync messages.
