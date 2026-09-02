# struct cGameSession

The single-player/multiplayer session controller. Global `g_GameSession` @
`0x84c9610` (game binary). Allocated `new(0x58)` and built by
`GameSession_Construct` (`0x817af70`, formerly `FUN_0817af70`). *Not* the
world/map — that's `g_World` (`0x85c0b74`).

## Layout (0x58 bytes)

| Offset | Type | Name | Meaning / evidence |
|--------|------|------|--------------------|
| `0x00` | `cTribe* [11]` | `tribes` | Faction/realm objects; 11 slots created in the ctor loop, each `new(0x84)`, init from roster template `DAT_08645240` (11 × 0x10). Indexed by tribe id (`session + id*4`). Class confirmed = `cTribe` (`cTribe::CloseBorders` string). |
| `0x2c` | `u8` | `bMultiplayerBattle` | `1` = MP/netgame battle mode; `0` = single-player. Set to 1 by `NetGame_InitBattle`, force-cleared to 0 by `SetupGame`. Enables the interactive command console (`InGame_HandleKeyCommand` case 0x21) + battle-stat views; disables some SP-only orders. |
| `0x2d` | `u8` | `localTribe` | Human player's tribe index. `0` in single-player (no selector); set from team-info / `g_LocalFactionTable` in multiplayer. |
| `0x2e` | `u8 [11]` | `tribeIndex` | Per-slot id table (slot `i` → `i`). |
| `0x39` | `u8 [3]` | `_pad0` | alignment |
| `0x3c` | `i32` | `tutorial` | Tutorial mode (0/1). Set by `GameSession_SetTutorial` (`"Initializing tutorial to %d"`). |
| `0x40` | `cArray<char*>* [2]` | `textTable` | Two localized string tables, each built by `LoadTextArrayFromFile` (reads a locale text file, one `strdup`'d line per entry). |
| `0x48` | `cGameInfo*` | `gameInfo` | Scenario data, loaded from `scenarioID` (`FUN_0817ad90`; `"cGameInfo::cGameInfo (Scenario invalid)"`). `NULL` when `scenarioID < 0`. |
| `0x4c` | `i32` | `scenarioID` | `-1` = MP battle (no scenario), `0` = SP campaign, `>0` = specific scenario. |
| `0x50` | `u8` | `bEditMode` | Edit mode, and as a side effect the simulation gate: `RealmGameLoop` steps `SimulationUpdate` only when this is `0`. Set once, at session construction (`GameSession_Construct`'s `startPaused` param, and `FUN_0817b610` from `LoadGame(path, editFlag)`), with 58 reads and zero writes through `g_GameSession` in the whole binary — so it never toggles at runtime, and since every call site passes normal mode it is unreachable as shipped. `SetupGame(1)` → edit, `SetupGame(2)` → normal ("Scenario edit mode" / "Scenario normal mode"). Console `save` refuses unless set; console `edit` only *reports* it. See [../subsystems/dev-console.md](../subsystems/dev-console.md#edit-mode). |
| `0x51` | `i32` | `gameSpeed` | Loaded from `.gamesettings` by `GameSession_LoadSettings` (default `0x50` = 80). Packed/unaligned at 0x51. |
| `0x55` | `u8 [3]` | `_pad1` | tail padding → 0x58 |

## C form
```c
struct cGameSession {          // 0x58
    cTribe*        tribes[11];      // 0x00
    uint8_t        bMultiplayerBattle; // 0x2c
    uint8_t        localTribe;      // 0x2d
    uint8_t        tribeIndex[11];  // 0x2e
    uint8_t        _pad0[3];        // 0x39
    int32_t        tutorial;        // 0x3c
    cArray<char*>* textTable[2];    // 0x40
    cGameInfo*     gameInfo;        // 0x48
    int32_t        scenarioID;      // 0x4c
    uint8_t        bEditMode;       // 0x50  (was named bPaused; see the table)
    int32_t        gameSpeed;       // 0x51 (unaligned/packed)
    uint8_t        _pad1[3];        // 0x55
};
```

## Notes
- The MCP can't create a Ghidra struct type; this layout is mirrored in a
  decompiler comment on `GameSession_Construct`. Apply it as a real struct in
  the Ghidra UI (Data Type Manager) if desired, then retype `g_GameSession`.
- `cTribe` side-finding: relationship state is a per-other-tribe array at
  `cTribe + 8 + otherId*4` (`cTribe_CloseBorders` writes `1` = borders
  open/known; Fatal `"These tribes ain't known by each other"`). Worth its own
  struct pass later.
- Consumers worth knowing: `cProvince_Do` (`0x81da420`) reads
  `bMultiplayerBattle`/`localTribe`/`scenarioID`/`bEditMode`;
  `SimulationStep`/`SimulationUpdate` gate on `bEditMode`;
  `InGame_HandleKeyCommand` gates the console on `bMultiplayerBattle`.
