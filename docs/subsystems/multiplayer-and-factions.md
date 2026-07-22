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
Two `cVOConsole`s:
- **`g_LogConsole`** (`0x85c0fe0`): output/log console, **Backspace = close** (`SetExitKey 0xe, mod 2`), auto-shown on output by `Console_ShowAndPrint` (`0x81f3fb0`). Backspace can only dismiss it after it self-shows.
- **`g_CmdConsole`** (`0x85c0f80`): interactive command console, opened by `InGame_HandleKeyCommand` **case 0x21** → `Edit`, **gated on `+0x2c != 0`**. ⇒ **usable in multiplayer battles only.**

To use the command console in single-player you'd have to force `g_GameSession+0x2c` non-zero (runtime patch) — nothing in the SP path enables it.

## Open threads
- The netgame **team-info packet** format consumed by `NetGame_AssignTeams` (maps network players → faction slots, mana/commander assignment — note `"Not a commander"`, `"There isn't the mana infos..."`).
- The faction roster template `DAT_08645240` (11 × 0x10) — decode fields (name/color/type/AI).
- Whether multiplayer is battle-only (tactical) vs. full strategic — the `scenarioID=-1` + battle-map load suggests **standalone tactical battles**.
