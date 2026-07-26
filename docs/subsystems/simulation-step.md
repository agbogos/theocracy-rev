# SimulationStep — one deterministic tick (game binary)

`SimulationStep(g_World)` @ `0x81f94a0`. Called `ticks` times per frame by `SimulationUpdate` (fixed timestep). This is the core gameplay update. Game executable (`0x08048000`).

## Player / realm model (confirmed)
From the session constructor `FUN_0817af70(session, scenarioID, startPaused)`:
- **`g_GameSession`** (`0x84c9610`): **11 faction slots** created (loop `0..0xa`), each `new(0x84)`, pointer at `+slot*4`, initialized from a static template table `DAT_08645240` (11 × 16 bytes) — the fixed faction roster. An index table sits at `+0x2e..+0x38`.
- `+0x2d` = **local/human player index** (ctor sets 0; `SetupGame` overwrites with `DAT_0864538a` — the chosen faction).
- `+0x2c` = **multiplayer/battle-mode flag**: `1` in netgame battles (set by `NetGame_InitBattle`), `0` in single-player (force-cleared by `SetupGame`). Enables the interactive command console + battle-stat views; disables some single-player-only orders. See [multiplayer-and-factions.md](multiplayer-and-factions.md).
- `+0x48` = `cGameInfo` (scenario data, loaded by `scenarioID`), `+0x4c` = scenario ID, `+0x50` = pause flag (starts 1 = paused).
- Different subsystems iterate different subsets of the 11 slots: **6** (`local_21 < 6`, the competing realms) for periodic province events, **8** elsewhere, **11** total. So "N players" depends on which system — the allocation is 11 faction slots (1 human + AI realms + neutral/special).
- **Provinces**: `g_World+0x1468` (count `+0x1470`); **province owner = byte at `province+0x40aae`** (index into the player array).
- **Order/command queue**: `g_World+0x83c` — `FUN_081a2060` reads the current tick's command. Feeding the sim from a queue is what makes it deterministic/lockstep-friendly.

## What one tick does
1. **cDayTime** stamps (profiling).
2. **Units-manager update** (unpaused only): pull the current command from the order queue (`g_World+0x83c`) and virtual-dispatch to the units manager (`g_World+0x1f394`). Then the **win/lose check** — if `g_World+0x140c → +0x4f4 == 0`, print `"### Most ki kene lepni ###"` (Hungarian: "should exit now").
3. **Per-player debug pass** (only if `DAT_084c9da6`): `FUN_0815af50` over the 8 slots, skipping the local player. (Debug output — low importance.)
4. **Manager-list update**: iterate `g_World+0x1490` (count `+0x1498`), virtual `+0x10` on each. These are the pluggable per-tick systems registered in `InitWorldForPlay`.
5. **Movement/transport update**: iterate `g_World+0x147c` (count `+0x1484`) → `UpdateMovementQueue` on each (below).
6. **Per-realm periodic event**: for each realm `0..5` (≠ local player), gated by diplomacy checks (`FUN_0815a1b0`/`0815a1e0`) and a **rate threshold**; when `(tick − base)` is past the threshold **and divisible by 7**, pick a **random province owned by that realm** (`Rnd(RandomServer)`) and call `TriggerProvinceEvent(province, localPlayer)`.

## `UpdateMovementQueue(node)` (`0x8129870`)
Per-tick movement of traveling entities along a path/node. For each entry in the node's queue (`node+0x18`, count `node+0x20`):
- Advance a countdown/progress counter (`entry+8`).
- **Arrived** (`counter < 1`): hand the carried object off into the `g_World+0x1490` container, clear the slot, remove from queue.
- **Still traveling**: interpolate the entity's **x/y position** toward the next node (`FUN_082d1230`). The x/y pair (two `u32`) lives on the **carried entity**, i.e. `*(entry+4) + 0x52` / `+0x56` — not on the queue entry itself.
- Safety: a divide-by-zero guard emergency-saves `save/as_save.tsg` then `Fatal` — i.e. a "this should never happen, dump state" assert.

(The x/y interpolation is the tell that this is spatial movement, not a build/production queue. Name is inferred; exact entity type TBD — likely people/goods moving between buildings/provinces.)

## `TriggerProvinceEvent(province, actingPlayer)` (`0x81d69a0`)
Fires on a randomly chosen province each cycle. Resolves the province **owner** (`g_GameSession[ province+0x40aae ]`), notifies the owner (`FUN_0815b000`/`0815afd0`), applies the event to the province (`FUN_081d6570`), and updates a province sub-object at **`+0x40e84`**. Concrete effect (unrest / random event / miracle) TBD — Theocracy is a god-game, so this is a strong candidate for the periodic "divine/random event" system.

> **Offset corrected (audit 2026-07-26).** This doc previously gave the sub-object
> as `+0x103a1`. That was the decompiler's **pointer-arithmetic index** on an
> `int *` parameter (`param_1 + 0x103a1`), i.e. `0x103a1 × 4` bytes. The raw
> instruction settles it: `081d69f3 ADD EBX,0x40e84`. Corroboration: `0x40e84`
> sits `0x3d6` past the owner byte at `0x40aae`, in the same province-header
> region, whereas `0x103a1` as a byte offset lands somewhere unrelated.
> **Watch for this whole class** — any offset lifted from a decompiled `TYPE *`
> parameter must be scaled by `sizeof(TYPE)`. Offsets taken off an `int`/`char *`
> base (like `+0x40aae` here, which is `*(byte *)((int)param_1 + 0x40aae)`) are
> already byte offsets and are fine.

## Determinism & lockstep (reinforced)
- The sim is driven by a **command queue** (`g_World+0x83c`), advances in **discrete ticks**, and uses a **single shared `RandomServer`** (seeded to `0x2a` = 42 in `Start`/menu paths). Command-queue input + shared seeded RNG + discrete ticks = a **deterministic, replayable, lockstep-synchronizable** simulation. Matches the per-tick `Send__10cMsgSender(...,2)` sync from `SimulationUpdate`.

## Open threads
- **Units manager** at `g_World+0x1f394` and the virtual it dispatches — the unit AI/movement core (biggest remaining piece).
- **`TriggerProvinceEvent` → `FUN_081d6570`** — decode the actual province event effect.
- `g_World+0x1490` manager list — enumerate the registered per-tick systems (what each vtable is).
- Province struct: map fields around `+0x40aae` (owner) and `+0x40e84`; confirm province record size/stride. (Both offsets are ~0x40000 in, so the province record is large — likely an embedded tile map ahead of the header fields.)
- Order-queue format at `g_World+0x83c` (`FUN_081a2060`/`FUN_081a1fa0`/`FUN_081a2180`) — the command/replay/mp-sync channel.
