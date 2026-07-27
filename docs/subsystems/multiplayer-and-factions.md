# Factions, Realm Selection & Multiplayer (game binary)

Answers "how many players", "can you pick your realm", and "when is the dev console usable". Addresses in the game executable (`0x08048000`).

## Faction roster — 11 slots
`GameSession_Construct(session, scenarioID, startPaused)` (`0x817af70`) allocates **11 faction slots** (loop `0..0xa`), each `new(0x84)`, pointer stored at `session + slot*4`, initialized from a **static roster template** `DAT_08645240` (11 × 0x10 bytes). So a match has up to 11 factions: the human + AI realms + neutral/special.

Iteration bounds seen elsewhere are just subsets of these 11:
- **6** — competing realms that get periodic province events (`SimulationStep`).
- **8** — a per-player maintenance pass.
- **11** — full roster.

## The local player / realm — `g_GameSession+0x2d`
- `GameSession_Construct` sets `+0x2d = 0`.
- **Single-player** (`SetupGame`): `+0x2d = g_LocalFactionTable[0]`. `g_LocalFactionTable` (`0x864538a`) lives in **`.bss` (zero)** and is **never written on the single-player path** ⇒ **the human is always faction 0. There is no single-player realm selector.** (Confirmed against play experience.)
- **Multiplayer** (`NetGame_AssignTeams`, `0x829c820`): `+0x2d` and the per-network-player slots are assigned from the netgame **team info** packet, indexed through `g_LocalFactionTable` (used here as a table, not a scalar). So the realm/slot-assignment machinery **exists but is wired only for multiplayer**.

## The `+0x2c` flag = multiplayer/battle mode
A single byte drives several mode differences:

| Path | `+0x2c` | Effect |
|------|--------|--------|
| `SetupGame` (single-player) | **0** | interactive console off; single-player orders enabled |
| `NetGame_InitBattle` (`0x829c630`) | **1** | interactive command console on; battle-stat views on; some SP-only orders disabled |

`NetGame_InitBattle` builds the session with `scenarioID = -1` (no campaign scenario), unpaused, loads a `LevelID` battle map (`new 0x40f80`). Region strings: `"Where is the team info in netgame?"`, `"hmm..keves a map :-o"` (Hungarian).

## Dev console — availability
Full write-up: **[dev-console.md](dev-console.md)**. Two `cVOConsole`s:
- **`g_LogConsole`** (`0x85c0fe0`): output/log console, `SetExitKey(0x0e, mask 2)` = **C**+qualifier, auto-shown on output by `Console_ShowAndPrint` (`0x81f3fb0`). It can only be dismissed after it self-shows. This is the console that receives a `cShell`.
- **`g_CmdConsole`** (`0x85c0f80`): interactive command console, opened by `InGame_HandleKeyCommand` **case 0x21** (= **Alt+V**) → `Edit`, **gated on `+0x2c != 0`**. ⇒ **reachable in multiplayer battles only** — and it never gets a shell, so a command typed into it is dropped by the null check in `cConsole::Process`. The interactive console is effectively vestigial as shipped, in MP too.

Single-player never sets `+0x2c`. `THEOC_CONSOLE=1` sidesteps the gate entirely — the host calls `Edit__10cVOConsole(g_LogConsole)` itself on Alt+V, which also works on the realm screen, where no key path reaches this dispatcher at all.

> **Corrected 2026-07-27.** The exit key `0xe` was previously read as
> **Backspace**; `eKeyCode` is not a PC scancode — Backspace is `0x36` and `0x0e`
> is **C**. See [re-methodology](../reference/re-methodology.md) §1.

## Netgame session lifecycle — `FUN_0829c300(netCtx, teamInfo)`

The whole multiplayer game, start to finish. Not called directly — it sits in a
function table (`0x85906a0`, `0x84bccb4`), as does `NetGame_AssignTeams`
(`0x85907d4`), so netgame entry points are dispatched, not hard-called.

1. `client = *(*(netCtx+0x34)+0xc)` → `printf("Client %p")`.
2. **`RandomServer` seeded to `0x2a` (42)** — on *both* ends. This is the concrete
   evidence for the lockstep model in [simulation-step.md](simulation-step.md):
   same seed + command queue + discrete ticks = reproducible on every peer.
3. `NetGame_InitBattle(netCtx)` — session with `+0x2c = 1`, `scenarioID = -1`,
   battle map (`new 0x40f80`).
4. `NetGame_AssignTeams(netCtx, teamInfo)` — below.
5. `netCtx+0x38 = 1`, then the battle loop (`FUN_080bb590`) with
   `RunInBackground = 1` around it.
6. Teardown: destroy `g_GameSession`, `printf("netgame vege")` (Hungarian for
   "netgame end"), then two leak assertions — `Fatal("ManIndexArray is not clean")`
   / `Fatal("BuildingIndexArray is not clean")`.

The `printf`s are useful runtime markers: `Client %p`, `PlayerCount: %lu`,
`playerid: %d`, `netgame vege` will all appear in our log during bring-up.

## Team-info packet format (decoded 2026-07-26)

Consumed by `NetGame_AssignTeams`; `NULL` → `Fatal("Where is the team info in netgame?")`.

```
+0x00  int32  playerCount
per player:
  +0x00  u8    playerId
  +0x01  ...   list of 7-BYTE RECORDS, terminated by one whose first int16 == -1
```

| Record field | Type | Meaning |
|---|---|---|
| `[0..1]` | `int16` | type — `0xffff` ends the list; `0x2a` selects the group/squad branch |
| `[2..3]` | `int16` | amount — resource quantity, or unit count |
| `[4..5]` | `int16` | *(unidentified)* |
| `[6]` | `u8` | flag — non-zero triggers an extra vcall `+0x90` on the spawned unit |

- The **first 5 records are the tribe's mana/resource pools**:
  `cTribe_AddResource(tribe, rec[i].amount, i)` for `i` in `0..4`. Fewer than five
  → `Fatal("There isn't the mana infos...")`.
- The **rest are units**, built by `FUN_0812c160(list, rec.type, tribe)`. Type
  `0x2a` takes the group branch; anything else must be a commander, else
  `Fatal("Not a commander")`. The finished list goes to the world object through
  a virtual call `(*(netCtx+4))->vtbl[0x18](…, list)`.

**playerId → tribe** resolves through the client's own slot table:
`slot = client + 0xc + playerId*0xc` (valid when `playerId < 8` and `slot[0] != 0`),
`tribe = DAT_084bcd8b[slot[8]]`, cached in `DAT_0867e232[playerId]`. After the
loop the **local** player's tribe is written to `g_GameSession+0x2d` via
`g_LocalFactionTable[tribe*2]` — the only path that ever writes `+0x2d` non-zero,
which is exactly why single-player always leaves the human as faction 0.

**Implication for the port:** this is all *post-receive* — `AssignTeams` parses a
buffer somebody already filled. Nothing here constrains the transport beyond
delivering bytes intact, so the socket layer does not need to understand the
protocol. Running the shipped `server` binary under emulation keeps both ends
original, so the format above is for *diagnosis*, not for reimplementation.

## Open threads
- The faction roster template `DAT_08645240` (11 × 0x10) — decode fields (name/color/type/AI).
- Whether multiplayer is battle-only (tactical) vs. full strategic — the `scenarioID=-1` + battle-map load suggests **standalone tactical battles**.
