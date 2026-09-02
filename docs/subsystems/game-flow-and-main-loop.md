# Game Flow & Main Loop (game binary)

Where the concrete application lives. Addresses are in the game executable (load
base `0x08048000`), not libmvos.

## Entry & the game↔engine handshake

- `main` @ `0x804fb44` is a PLT thunk (`JMP [GOT]`) into libmvos. The generic
  bootstrap lives in the engine, and calls back into two game-provided symbols,
  which libmvos imports as `Init`/`Start`:
  - `cApplication::Init` @ `0x8144600` — sets every requirement flag (`Sound`,
    `Video`, `Mouse`, `Keyboard`, `Redbook`, `Network`, `Pointer`, `Timer`,
    `Intuition`, all = 1). Theocracy uses the whole engine.
  - `cApplication::Start(argc, argv)` @ `0x8144650` — the top-level state
    machine, below.

The init order is therefore: engine `.so` load (global ctors) → engine bootstrap
→ `Init()` → engine opens the required subsystems → `Start()`.

## `cApplication::Start` — top-level state machine

1. **Config:** load `data/selap.txt`.
2. **Single-instance lock:** open IPC on `localhost:5043` (port `0x13b3`); if
   taken → `Fatal("...You can run only one Theocracy in the same time!")`.
3. **Logo/intro movies** via `PlayMovie` (SMPEG): `ubi_logo.mpg` (Ubi Soft
   publisher, skippable), `logo.mpg`, `intro.mpg`.
4. **Audio:** allocate `cSoundServer` (`g_SoundServer` @ `0x84c9304`) with 16
   `cSoundServerChannel`s.
5. `ActivateScreen(Intuition)` → show UI.
6. **Menu loop:** `sel = MainMenu_Run()`; `switch(sel)` until `sel == 8` (quit):

   | id | Menu | Action |
   |----|------|--------|
   | 1 | Single Player | `OpenDisplay(800×600)` → `SetupGame(2)` → `OpenRealmScreen` |
   | 2 | Multiplayer | `FUN_0829bf80` |
   | 3 | Intro | `OpenDisplay` → `PlayMovie("intro.mpg")` |
   | 4 | Load Game | load dialog `FUN_081a07f0` → `OpenRealmScreen` |
   | 5 | Credits | `FUN_08145920` |
   | 6 | Help | `FUN_08131400` |
   | 8 | (quit) | exit loop |
   | 9 | Demo | `SetupGame(2)` → `OpenRealmScreen` (attract mode) |
   | 10 | Tutorial | `FUN_08145550` |
   | 0xb | Scenario | `FUN_08145550(2,0)` |

   An else-branch (`DAT_084c930d != 0`) direct-launches a game and skips the
   menu — the command-line auto-start path.
7. **Cleanup:** `CloseAllInputFiles`, release the IPC single-instance lock.

## Key routines (named in Ghidra)

- `MainMenu_Run` (`0x812e980`): builds & runs the main menu. Sets 800×600,
  loads `data/menu/menu.cfg`, prints `"Inint menu buttons begin ..."` (sic), and
  creates a `cVOAButton` for each of `single`, `multi`, `intro`, `load`,
  `credits`, `help`, `exit`, `tutorial` and `scenario`, each with localized text
  (`cLocaleEntry`) and a click `cSample`. Builds the
  `cVObject`/`cMasterVO` widget tree
  (`CalcAbsCoordTree`/`RefreshTree`/`InitTree`), then an event loop reading a
  `cVObject` event pipe; returns the pressed button's id. Exit (id 7) spawns a
  confirm dialog → returns 8.
- `SetupGame(mode)` (`0x81457e0`): prepares a session.
  - `mode 0` (new game): reset `ManIndexArray` (population and units) and
    `BuildingIndexArray` (buildings), with a `Fatal` if either is not clean,
    then `new` the world object → `g_GameSession` (`0x84c9610`).
  - `mode 1`: load from save (`FUN_081a07f0`). `mode 2`: load + `FUN_081f9430`.

  These `*IndexArray`s are the simulation's entity registries.
- `OpenRealmScreen` (`0x8146010`): enter gameplay. Sets palette + pointer
  sprite on `g_RealmScreen` (`0x84c9128`), `ActivateScreen` at depth 5
  (fallback 4), else `Fatal("unable to open realm screen")`. "Realm" is the
  in-game world.
- `PlayMovie(file, skippable, ?, rect)` (`0x817bbe0`): retries open 3×, sets
  640×480, calls `External_PlayAnim` (SMPEG). Prints `"TheocracyMovie[...]"`.

## Named globals
- `g_GameSession` @ `0x84c9610` — session/world object (created by `SetupGame`).
- `g_SoundServer` @ `0x84c9304` — `cSoundServer`, 16 channels.
- `g_RealmScreen` @ `0x84c9128` — the in-game `cScreen`.

## Open threads
- **`OpenRealmScreen` → gameplay:** `FUN_081a67a0` / `FUN_081f9bb0` are the next
  hop into the actual sim/render loop — trace these for the real per-frame game
  loop.
- `ManIndexArray` / `BuildingIndexArray` layout — the entity model (units &
  buildings).
- `FUN_08145550` (tutorial/scenario launch) and `FUN_0829bf80` (multiplayer
  setup).
- The command-line direct-launch path (`DAT_084c930d`/`DAT_084c9314`) — how a
  game is auto-started.
