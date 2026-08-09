# The starting world — what actually ships in `init.dat`

Every map's opening state is a **savegame**, not code. `data/campaign/init.dat`
and all eight `data/scenario/scn*/init.dat` carry the `theosg42` magic at offset
64 and go through the ordinary `LoadGame` path. So the men, the heroes and the
magic items a player starts with are *loaded from a file*, and a sweep of call
sites — however exhaustive — cannot see any of them.

That is the correction this doc exists to make durable. It closes the last open
question of [heroes.md](heroes.md) and [magic-items.md](magic-items.md), and the
"created by no code path" wording that [missions.md](missions.md) had to leave
standing.

**Read off `theocracy.real` 2026-08-09.** Addresses are Ghidra space, game base
`0x08048000`. Findings are mirrored into the Ghidra DB as renames and comments.

## The load chain

`LoadGame` is `0x081a07f0`. It takes an optional path — the slot dialog supplies
one interactively, and `SetupGame` passes `"<mapdir>/init.dat"` for a new game.

```
LoadGame(path)                                    0x081a07f0
  ├─ Read(header, 0x48)                           72 bytes; magic at +64
  ├─ cGameSession_ctor_fromStream                 0x0817b610
  │    ├─ u32 scenario id  ──> selects the cGameInfo (0x0817ad90, ids 0..8)
  │    ├─ u8  local faction
  │    ├─ 11 × [ cTribe_ctor_fromStream (0x08159b20) + u8 ]
  │    ├─ u32
  │    ├─ iScenario_Load                          0x08225950, cGameInfo vtable +0x14
  │    │    ├─ u8
  │    │    ├─ cMissionHandler_Load               0x0820fcd0
  │    │    └─ cWorld_ctor_fromStream             0x081fba80, object 0x1f3b4 bytes
  │    └─ 11 × tribe part two (0x08159de0)
  └─ (header check, ManIndexArray/BuildingIndexArray cleanliness asserts)
```

Two details worth having written down.

**The header check compares the magic with itself — and works anyway.** The
function copies the eight literal bytes into `local_2c`, then reads 0x48 bytes
into `local_6c[64]`, which is the *adjacent* stack slot: the read deliberately
overruns into `local_2c` and replaces the copy with the file's own bytes before
the comparison. It reads as a tautology in the decompiler and is not one. The
72-byte header is otherwise unread — consistent with
[save-format.md](save-format.md), where it is 54 bytes of uninitialised stack.

**The file names its own scenario.** `cGameSession_ctor_fromStream` reads the
scenario id out of the stream and builds the matching `cGameInfo` from it, so a
world file is self-identifying. That is what makes `THEOC_WORLD_FILE` (below)
sound: serving `scn3/init.dat` for the campaign's open still loads it as
scenario 3.

### `LoadRegistredObject` — the format is self-describing

`0x08073f50`. Everything polymorphic in the world goes through it, and it is the
reason a savegame can be navigated at all without a complete struct map:

```
[u32 len][len bytes: class name, NUL included]  then that class's stream ctor
```

The registered names are `cUnitCMDLast`, `cUCMDGoTown`, `cUCMDTenting`,
`cUCMDEnter`, `cUCMDWait`, `cUCMDTrade`, `cProvince`, `cRoad`, `cRoadUnitItem`,
`cCross`, `cTown`, `cRealmUnit`, `cNewLetterMsg`, `cMsg`, `cLetterMsg`. Anything
else is `Fatal("LoadRegistredObject: ERROR: Unregistred object!")`.

`cProvince` is the big one — `new(0x40f80)`, constructed by `0x081ebaa0`, and
returned with the pointer adjusted by `+0x1003b` words, which the array loaders
un-adjust. The world holds provinces in the polymorphic array at `world+0x1468`.

### How a man is born from a file

Inside `cProvince_ctor_fromStream`, eleven `cManList_Load` calls (`0x08147540`,
one per tribe) each read a leader count, then per leader a count of inferiors,
then a flat trailing list. Every one of those men comes from
**`CreateMan_fromStream`** (`0x081bece0`):

```c
Read(stream, &caste, 1);                       // 0..0x29 — 42 castes
if (caste > 0x29) Fatal("CreateMan : Unknown caste");
(*(caste_props[caste] + 0xc))(stream);         // the per-caste creator
```

`caste_props` is `PTR_DAT_084c9ee0`, 42 pointers into `.bss`. **The creator
function pointers are written at runtime** — `FUN_08254570` stores `FUN_082543b0`
into `0x0866d828`, which is caste `0x21` + `0xc` — so this is an indirect call
with no rel32 anywhere. That is precisely why the man classes appear in no xref
sweep, and it is the mechanical cause of the "hero X is created by nothing"
error. Castes 9, 11 and 12 have a NULL entry and cannot be loaded at all.

Of the 42 castes, **only 33, 34 and 35 read an extra byte** — checked against
every caste constructor, not assumed. Those are the three hero man types from
[heroes.md](heroes.md), and the byte is the hero id:

```
cHero_ctor_fromStream                0x080b22c0
  ├─ cMan_ctor_fromStream            0x080aeb10 → 0x08094730
  ├─ vtable = 0x0831af20
  └─ Read(stream, this+0x27c, 1)     ← the hero id, straight from file data
```

Magic items arrive the same way, one level down: the base `cMan` constructor
reads a `u16` item count and calls `Item_CreateFromStream` (`0x0820dbb0`) that
many times, which reads one id byte and hands it to `Item_CreateById`
(`0x0820d1f0`). A world file can therefore materialise **any of the fifty**.

## Reading it out with the game's own loader

The load chain above is roughly 150 stream constructors deep, and a
re-implementation would have to be complete before it could be trusted at all.
The game already contains a correct parser, so the port watches that one instead.

`THEOC_DUMP_WORLD=1` installs three **passive** watches — `Machine::add_watch`,
which observes one guest instruction and changes nothing:

| address | what | read |
|---|---|---|
| `0x081a07f0` | `LoadGame` entry | the path at `[esp+4]`; every event after it belongs to that file |
| `0x081becfc` | `CreateMan_fromStream`, just past the caste read | the caste byte at `[ebp-1]` |
| `0x080b22f6` | `cHero_ctor_fromStream`, just past the id read | the hero id at `EBX+0x27c` |
| `0x0820d1f0` | `Item_CreateById` entry | the id at `[esp+4]`, **and the return address at `[esp]`** |

Two choices in there are the ones that matter.

**Watch `Item_CreateById`, not `Item_CreateFromStream`.** The factory is the
single choke point all fifty item classes pass through on the load path, so no
subclass can be missed by forgetting to hook it. The cost is that it also
catches config placement, console commands and cheats — which is why the return
address is recorded: `0x0820dbd5` is the call site *inside*
`Item_CreateFromStream`, i.e. "this item came out of the world file". Everything
else is counted separately rather than filtered away, because "who else creates
items" is the same question one level up.

**The caste watch is the control.** Men are the one thing a world file certainly
contains in bulk, so a run reporting zero men means the instrument is not firing,
not that the world is empty. Without it, "0 heroes" and "instrument broken" look
identical — and on the first run of this instrument they did.

`THEOC_WORLD_FILE=<path>` serves one chosen file for every `init.dat` the guest
opens. Which map's world gets loaded is otherwise decided by a menu the
unattended harnesses cannot drive; the redirect is sound because the file names
its own scenario id (above). Both knobs are diagnostics — nothing is patched and
the guest runs unchanged.

The whole sweep is nine headless runs, no display:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  THEOC_SKIP_MOVIES=1 THEOC_AUTO_PROVINCE=1 THEOC_DUMP_WORLD=1 THEOC_START_SEC=40 \
  THEOC_WORLD_FILE="$PWD/data/game/data/scenario/scn3/init.dat" \
  ./port/build/theoc 2>&1 >/dev/null | grep '^\[world\]'
```

Note the `LoadGame(...)` line prints the **guest** path, so under
`THEOC_WORLD_FILE` it still says `data/campaign/init.dat`; the redirect itself is
logged once as `[world] THEOC_WORLD_FILE: '<guest>' -> '<host>'`.

## The census

All nine world files, read by the game's own loader (2026-08-09):

| file | men | heroes | magic items |
|---|---:|---|---|
| `campaign` | 1248 | 1, 2, 4, 6, 7, **11**, 12, 13, 14, 15, 16, 18 | 3, 4, 5, 9, 10, 14, 15, **16×2**, 20, 22, 27, 32, 33, 35, **36×2**, 38, 39, 42, 44, 47 |
| `scn1` | 26 | — | — |
| `scn2` | 216 | — | — |
| `scn3` | 582 | 6 | 3, 36 |
| `scn4` | 266 | 2 | 10 |
| `scn5` | 270 | 18 | 33 |
| `scn6` | 56 | **19** | 46 |
| `scn7` | 978 | 12 | 14, 35 |
| `scn8` | 183 | — | 3 |

### The three questions this was run to answer

1. **Jarakhi (hero 11) is in `data/campaign/init.dat`** — confirmed. He is in no
   other file, appears in no `hero.cfg` row and is handed out by no mission: the
   campaign's player character reaches the game exactly one way, as save data.
   [heroes.md](heroes.md)'s claim is now read off the running loader rather than
   inferred from his absence everywhere else.
2. **Tlechlal (19) is in `scn6/init.dat`** — the last unexplained hero, and the
   one nobody had a story for. He is a scenario-6 fixture, not campaign content,
   which is why every campaign-shaped search missed him.
3. **Mask of the Brave (item 1) is in none of the nine worlds.** With config
   placement, mission placement and now all nine world files checked, the
   stronger claim stands: **it is dead in the shipped game.** The withdrawal
   `magic-items.md` and `missions.md` recorded on 2026-08-09 is lifted.

**Umochi (hero 9) is in no world file either.** Every channel is now checked for
him — `hero.cfg`, mission rewards, and serialised state — and he is in none. The
placeholder reading in [heroes.md](heroes.md) is as strong as it can get.

### Four of the fourteen "no code path" items do ship

Of the fourteen items [magic-items.md](magic-items.md) found no construction site
for — 1, 2, 7, 9, 12, 17, 18, 29, 32, 41, 44, 47, 49, 50 — **four are in the
campaign world**: 9 (Immortal Shield), 32 (Grappling Spears), 44 (Earring of
Balance) and 47 (Symbol of Power). They were placed with an editor and saved.

**Ten are in nothing**: 1, 2, 7, 12, 17, 18, 29, 41, 49, 50. Nine of the ten are
fully implemented items that no shipped content hands out; only item 1 is also
inert. A player can still summon any of them from the developer console.

### The campaign world looks generated, then edited

The campaign census lines up against the two config placers with a precision
that is hard to read any other way:

- Its twelve heroes are **exactly `hero.cfg`'s eleven, plus Jarakhi**.
- Its items are the `hero.cfg` ∪ `mitem.cfg` union minus 30 and 40, plus the
  four editor-placed ones above.
- **Items 16 and 36 appear twice**, and 16 and 36 are precisely the two ids that
  appear in *both* config files.

So `data/campaign/init.dat` reads as the output of starting a new game — running
the placement the config files describe — then editing in a player character and
a handful of extra items and saving. That makes `hero.cfg` and `mitem.cfg`
authoring inputs whose effect reaches players through the world file, which is a
different thing from being loaded at play time.

Two cautions on the last point, both about what was *not* measured. No item was
created outside the stream during any run — placement never fired — but every
run entered through the campaign menu, so this says nothing about what a
scenario started from its own menu entry does. And each run observed only boot →
province, about 40 seconds, so placement later in a session is not excluded.

## Open threads

- **`hero.cfg` / `mitem.cfg` at play time.** When, if ever, do
  `HeroCfg_PlaceHeroes` (`0x08215200`) and `MitemCfg_PlaceItems` (`0x08214e30`)
  actually run? The gate is `*(int *)(g_GameSession + 0x4c) == 0`, and `+0x4c` is
  the scenario id read from the world file, which is `0` for the campaign — so
  the condition is *true* for the very path where nothing was observed to fire.
  Either the call site is reached under some other condition, or it is reached
  later than these runs went. Reading the caller settles it.
- **Items 30 and 40** (Jaguar Killer, Falcon Blade) are placed by `mitem.cfg` —
  provinces 16 and 41 — and are in no world file. Under the "generated, then
  edited" reading they were deliberately removed, but that is inference.
- **The rest of the record layout.** This doc pins the *chain*, not the bytes.
  The 58 building classes, the per-caste `cMan` extras and the mission/timer
  loaders behind `cMissionHandler_Load` are mapped only as far as "these are the
  readers". §16 of [re-methodology.md](../reference/re-methodology.md) says why
  that was the right place to stop, and when it would not be.
- **`cMissionHandler_Load` is a lead for [missions.md](missions.md)'s open
  task.** It loads a day count, then four timer objects and twelve missions
  through their own vtable slot `+0xc` — i.e. missions are *serialised with the
  world*, which is a different answer from "constructed by the campaign" and
  bears on "what starts a mission".
