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

One exception, found later and narrow: the *pacing* is not frame-tied, but at
the fast-forward speeds the achieved calendar rate is, because
`SimulationUpdate` discards the sub-tick remainder on any frame that ran a
tick. See "Game speed, pause and fast-forward" below.

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

## Game speed, pause and fast-forward

**There is no separate fast-forward loop.** `RealmGameLoop` is the only
empire/map loop, `SimulationStep` has exactly one caller (`SimulationUpdate`,
checked by xref), and fast-forwarding is the catch-up `while` above running
more ticks per frame.

Speed is one field: `tickDuration` (`g_World+0x1408`), **milliseconds per
tick** — and a tick is a day, so the UI unit is *days per second*. The whole
set of writes was enumerated by scanning for the displacement
([re-methodology](../reference/re-methodology.md) §17): three writes, two
reads, and two `lea`s where `cWorld::Save`/`Load` take its address.
The default is the only literal, written by the world builder (`0x81fb67a`):
`0xa6` = 166 ms, so **the default empire speed is ~6 in-game days per second**.
The other two writes are:

| Where | What it does |
|---|---|
| `SetGameSpeed(world, daysPerSec)` (`0x81f9390`) | `tickDuration = 1000/daysPerSec`. `0` pauses, `>= 1` resumes a stopped clock and then sets the rate, `< 0` returns without touching anything — the "leave it alone" value |
| `GameSpeedPanel_HandleMsg` (`0x81af5a0`) | the panel owning the pause button (child `+0x7c`) and the speed slider (child `+0x16c`); the slider branch writes `1000/value` straight into `tickDuration`, bypassing `SetGameSpeed` |

and the read that is not `SimulationUpdate`'s divisor is `GameSpeedSlider_Do`
(`0x81af620`), the reverse direction: on message `0x40` it moves the knob to
match `tickDuration`, and returns `GameSpeed_LockDepth != 0` so the widget can
render itself as locked. One further caller of `SetGameSpeed` is the
message-bar handler at `0x8069f40`, which applies a per-message speed from a
runtime-filled table at `0x859b940` (stride `0x44`) — the mechanism by which an
event notification can drop the game speed. What populates that table has not
been read.

### The keys are B, N and M

`eKeyCode` `0x0d`/`0x19`/`0x18` → `SetGameSpeed(1)`, `(50)` and `(100)` days
per second, in `RealmMapView_Do` (`0x81a96f0`) via the jump table at
`0x8380060` indexed by `key - 0x0d`. The mapping was read out of the table
bytes rather than trusted from the decompiler's case labels
([re-methodology](../reference/re-methodology.md) §5), and `eKeyCode` is
libmvos's dense enum (`a` = `0x0c`), not a PC scancode — cross-checked against
the port's own `sdl_scancode_to_ekey` table and against `v` = `0x21` for Alt+V.
Each is gated on `GameSpeed_LockDepth(world) == 0`. All three sit *above* the
`0x84c9123` debug gate in that function, so they are shipped player controls
rather than cheats.

### Pause is the timer, not the flag

`RealmGameLoop`'s `if (g_GameSession+0x50 == 0)` gate is **edit mode**, and it
never toggles at runtime ([../structs/cGameSession.md](../structs/cGameSession.md)).
The real pause is the `cGameTimer` at `g_World+0x1410`, a class that names
itself in its own error string — `"E: cGameTimer::Unlock() : object is not
locked\n SZOLJ asvanynak!"`, Hungarian, and a colleague's name
([re-methodology](../reference/re-methodology.md) §9).

It is a nestable stopwatch over `cDayTime`: `+0x10` holds the start timestamp
while running and the frozen elapsed value while locked, `+0x14` is the lock
depth.

| | |
|---|---|
| `cGameTimer_Lock` (`0x81f5890`) / `_Unlock` (`0x81f58c0`) | freeze / thaw, nested. `_UnlockAll` (`0x81f5900`) forces the depth to 0 |
| `cGameTimer_GetElapsedMs` (`0x81f5830`) | **returns 0 while locked** — that is the entire mechanism |
| `cGameTimer_NowMs` (`0x81f5760`) | `SetBySys__8cDayTime` then `sec*1000 + usec/1000`, which is what fixes `tickDuration`'s unit as milliseconds |
| `World_PauseClock` (`0x81f93f0`) / `_ResumeClock` (`0x81f9430`) / `_ResumeClockAll` (`0x81f9470`) | the same on `g_World`, plus a `cMsgSender` notification: `0` on pause, `1` on resume |
| `PauseGame` (`0x81fcb00`) / `ResumeGame` (`0x81fcb20`) | nestable *game* pause — they also inc/dec a depth counter at `g_World+0x5c0`, read by `GameSpeed_LockDepth` (`0x81fcb40`) |

So a paused game still calls `SimulationUpdate` every frame: elapsed is 0, it
computes 0 ticks, and returns having done nothing. `SetupGame` mode 2 calling
`World_ResumeClock` ([game-flow-and-main-loop.md](game-flow-and-main-loop.md))
is a loaded save starting its clock.

The depth at `g_World+0x5c0` starts at 0 (`0x81fb5c8`) and is saved with the
world ([calendar.md](calendar.md) places it in the `.tsg` stream). Its only
caller pair is `Province_PauseEmpireClock` / `_ResumeEmpireClock`
(`0x81db1a0` / `0x81db1d0`), which stop and restart the empire calendar once on
province entry and exit, guarded by the byte at `province+0x40a31`. That is why
empire time freezes while you are inside a province, and why B/N/M do nothing
there.

### Fast-forward loses days, and this one is frame-rate dependent

`cGameTimer_Reset` (`0x81f5860`) rebases the timer after a batch of ticks, and
the call sits **inside** `SimulationUpdate`'s `if (ticks != 0)` branch, so the
sub-tick remainder is discarded — but only on frames that ran at least one
tick. At the default 166 ms/day a frame is far shorter than a tick, nothing is
discarded, and time accumulates exactly. At M (100 days/sec, `tickDuration` =
10 ms) a 16 ms frame yields one tick and throws away 6 ms, so the calendar runs
*below* the nominal rate in fps-sized quanta: nominal 100 days/sec comes out
nearer 60 at 60fps. The 10-tick clamp only bites past 100 ms/frame.

For the port this is the one place where realm frame rate changes game
behaviour rather than just smoothness, and it is original behaviour, not
something the emulator introduces.

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

Named with the game-speed pass: `SetGameSpeed`, `PauseGame`, `ResumeGame`,
`GameSpeed_LockDepth`, `World_PauseClock`, `World_ResumeClock`,
`World_ResumeClockAll`, `Province_PauseEmpireClock`,
`Province_ResumeEmpireClock`, `RealmMapView_Do`, `GameSpeedPanel_HandleMsg`,
`GameSpeedSlider_Do`, and the `cGameTimer` methods `_NowMs`, `_IsLocked`,
`_GetElapsedMs`, `_Reset`, `_Lock`, `_Unlock`, `_UnlockAll`.

## Open threads (next targets)
- **`SimulationStep` (`0x81f94a0`)** — the single deterministic tick; decompile
  to find the unit/AI/economy update. This is the core gameplay logic.
- **Unit model:** `g_World+0x1f398` container + `FUN_082cd2a0`/`FUN_082cc030`
  accessors → the `cMan`/unit struct.
- **Province/building model:** `g_World+0x1468`, `+0x147c`, `+0x1490`.
- **The `cMsgSender` at `+0x5c8`** — decode what message `2` carries. This is
  now the *only* candidate for the command/sync channel, since
  `FUN_081a1fa0`/`FUN_081a2180` turned out to be `cDate_ctor_YMD`/`cDate_Add`.
- Confirm lockstep by checking the IPC receive side consuming the tick-sync
  messages.
