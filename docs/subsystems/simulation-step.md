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
- **Game date**: `g_World+0x83c` is the `cDate` instance — see [calendar.md](calendar.md). Not a command queue; see the correction below.

## What one tick does
1. **cDayTime** stamps (profiling).
2. **Units-manager update** (unpaused only): compute the **current day count** — `cDate_ToDayCount(g_World+0x83c)` — and pass it to the units manager (`g_World+0x1f394`, virtual `+0x10` on its `+0x28` sub-object). The units manager is driven by *the date*, not by a command. Then the **win/lose check** — if `g_World+0x140c → +0x4f4 == 0`, print `"### Most ki kene lepni ###"` (Hungarian: "should exit now").
3. **Per-player debug pass** (only if `DAT_084c9da6`): `FUN_0815af50` over the 8 slots, skipping the local player. (Debug output — low importance.)
4. **Manager-list update**: iterate `g_World+0x1490` (count `+0x1498`), virtual `+0x10` on each. These are the pluggable per-tick systems registered in `InitWorldForPlay`.
5. **Movement/transport update**: iterate `g_World+0x147c` (count `+0x1484`) → `UpdateMovementQueue` on each (below).
6. **Per-ally periodic event**: for each realm `0..5` (≠ local player), gated by `cTribe_IsKnown` **and** `cTribe_IsAllied` against the local player. The elapsed quantity is `cDate_ToDayCount(g_World+0x83c) − cTribe_GetAllyDate(localTribe, realm)` — **days since the alliance was formed**, not ticks since some base. The threshold is `(g_TicksPerYear / g_TicksPerDay) × DAT_084c8160` = `365 × ALLIED_JOIN_YEARS`. When elapsed is past it **and divisible by 7**, pick a **random province owned by that ally** (`Rnd(RandomServer)`) and call `TriggerProvinceEvent(province, localPlayer)`. See "What this event actually is" below.

## `UpdateMovementQueue(node)` (`0x8129870`)
Per-tick movement of traveling entities along a path/node. For each entry in the node's queue (`node+0x18`, count `node+0x20`):
- Advance a countdown/progress counter (`entry+8`).
- **Arrived** (`counter < 1`): hand the carried object off into the `g_World+0x1490` container, clear the slot, remove from queue.
- **Still traveling**: interpolate the entity's **x/y position** toward the next node (`FUN_082d1230`). The x/y pair (two `u32`) lives on the **carried entity**, i.e. `*(entry+4) + 0x52` / `+0x56` — not on the queue entry itself.
- Safety: a divide-by-zero guard emergency-saves `save/as_save.tsg` then `Fatal` — i.e. a "this should never happen, dump state" assert.

(The x/y interpolation is the tell that this is spatial movement, not a build/production queue. Name is inferred; exact entity type TBD — likely people/goods moving between buildings/provinces.)

## `TriggerProvinceEvent(province, actingPlayer)` (`0x81d69a0`)
Fires on a randomly chosen province each cycle. Resolves the province **owner** (`g_GameSession[ province+0x40aae ]`), notifies the owner (`FUN_0815b000`/`0815afd0`), applies the event to the province (`FUN_081d6570`), and updates a province sub-object at **`+0x40e84`**. Concrete effect still TBD, but its **only direct call site is the `ALLIED_JOIN_YEARS` site above** (`0x081f97ab` in `SimulationStep`), so whatever it does is part of the alliance mechanic — the earlier "divine/random event" guess is withdrawn (see "What this event actually is"). One **data** reference also exists, at `0x0852d6f8`, unexamined; if that is a vtable slot the function has a second, virtual, caller and the "only" above is too strong.

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

## Determinism & lockstep (weakened — corrected 2026-08-06)

> **The command queue does not exist.** This section previously rested on three
> legs: a command queue at `g_World+0x83c`, discrete ticks, and a shared seeded
> RNG. The first was a misreading — `g_World+0x83c` is the `cDate` game date and
> the three functions cited as the queue's API are date arithmetic
> ([calendar.md](calendar.md)). Nothing in `SimulationStep` reads a command from
> anywhere.

What survives, and it is less than was claimed:

- The sim advances in **discrete ticks** — still true, and `SimulationUpdate`'s
  fixed timestep is real.
- A **single shared `RandomServer`** (seeded to `0x2a` = 42 in `Start`/menu
  paths) — still true, and still the strongest determinism evidence here.
- `Send__10cMsgSender(world+0x5c8, 2)` is sent **once per `SimulationUpdate`
  call that ran at least one tick**, not once per tick — it sits *after* the
  while loop. Since a call runs 0–10 ticks, the message count and the tick count
  are not the same number, so it cannot by itself be a per-tick sync.

So "deterministic, replayable, lockstep-synchronizable" is a **hypothesis
consistent with the evidence, not something read off the code**. A lockstep
design needs a channel that carries player commands between peers; this doc
claimed to have found it and had not. The `cMsgSender` at `+0x5c8` is still the
best candidate and is still undecoded. Until its payload is read, the honest
statement is: the sim *could* be lockstep and nothing contradicts it.

## What this event actually is (step 6)

`DAT_084c8160` is not an anonymous rate constant. It is bound by name at
`0x080b3f32` — `RegisterConfigVar(&DAT_084c8160, "ALLIED_JOIN_YEARS")` — to a
tunable in the balance file, and the shipped `data/selap.txt` sets
**`ALLIED_JOIN_YEARS=10`**.

Read with that name, step 6 is: **once you have been allied with a realm for 10
in-game years, one random province of that ally is picked every 7 days and put
through `TriggerProvinceEvent`.** The obvious reading of "allied join" is that
those provinces gradually *join* your realm — a long alliance annexing itself to
you one province at a time.

**That is a hypothesis, not a finding.** The effect itself is `FUN_081d6570`,
still undecoded; the config name is strong evidence about intent but is not the
code. What it does settle is that this is a **diplomacy mechanic keyed to
alliance age**, not the "periodic divine/random event system" this doc
previously guessed at — a god-game prior that the binary does not support.

## Open threads
- **Units manager** at `g_World+0x1f394` and the virtual it dispatches — the unit AI/movement core (biggest remaining piece).
- **`TriggerProvinceEvent` → `FUN_081d6570`** — decode the actual province event effect.
- `g_World+0x1490` manager list — enumerate the registered per-tick systems (what each vtable is).
- Province struct: map fields around `+0x40aae` (owner) and `+0x40e84`; confirm province record size/stride. (Both offsets are ~0x40000 in, so the province record is large — likely an embedded tile map ahead of the header fields.)
- **Where player commands actually enter the sim.** The order queue this doc used to cite was `cDate` all along, so the question is open again and nothing is known about it. `cMsgSender` at `g_World+0x5c8` is the candidate; decoding its payload would settle the lockstep hypothesis above.
- **`FUN_081d6570`** — the `ALLIED_JOIN_YEARS` effect. Does an allied province actually change owner? The `+0x40aae` owner byte is the thing to watch.
- **`0x0852d6f8`** — the data reference to `TriggerProvinceEvent`. Vtable slot or plain table?
