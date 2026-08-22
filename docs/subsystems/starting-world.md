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
function pointers are written at runtime** — `FUN_08254570` stores
`FUN_082543b0` into `0x0866d828`, which is caste `0x21` + `0xc` — so this is an
indirect call with no rel32 anywhere. That is precisely why the man classes
appear in no xref sweep, and it is the mechanical cause of the "hero X is
created by nothing" error. Castes 9, 11 and 12 have a NULL entry and cannot be
loaded at all.

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
The game already contains a correct parser, so the port watches that one
instead.

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
contains in bulk, so a run reporting zero men means the instrument is not
firing, not that the world is empty. Without it, "0 heroes" and "instrument
broken" look identical — and on the first run of this instrument they did.

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
`THEOC_WORLD_FILE` it still says `data/campaign/init.dat`; the redirect itself
is logged once as `[world] THEOC_WORLD_FILE: '<guest>' -> '<host>'`.

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

Of the fourteen items [magic-items.md](magic-items.md) found no construction
site for — 1, 2, 7, 9, 12, 17, 18, 29, 32, 41, 44, 47, 49, 50 — **four are in
the campaign world**: 9 (Immortal Shield), 32 (Grappling Spears), 44 (Earring of
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

## The campaign builder, recovered

The shipped game contains the tool its own worlds were made with. Both launchers
take a mode argument, and the developers' names for the three values are in
their own `printf` strings:

| | `SetupGame(mode)` — Prophecy, `0x081457e0` | `FUN_08145550(mode, id)` — Chronicles, `0x08145550` |
|---|---|---|
| **0** "init mode" | `GameSession_Construct(new(0x58), 0, paused=1)` | `GameSession_Construct(new(0x58), id, paused=1)` |
| **1** "edit mode" | `LoadGame("<map>/init.dat", paused=1)` | same |
| **2** "normal mode" | `LoadGame("<map>/init.dat", paused=0)` | same |

The menu sends **2**. Mode 0 is the only one that never touches `init.dat`: it
runs the **non-stream** `cWorld` constructor (`0x081fc4b0`), which leaves
`world+0x5b4` at zero — and that byte is the whole load-vs-generate fork.
`FUN_081f99b0` branches on it:

```
FUN_081f99b0(world)              0x081f99b0   realm init, both paths
  world+0x5b4 == 0  →  FUN_081fb5b0   BUILD:   realm from /realm/realm.raw,
                                               seed the AI tribes, place
                                               mitem.cfg + hero.cfg, date := 1323/07/04
  world+0x5b4 != 0  →  FUN_081fb170   RESTORE: the world came from a file
```

`cWorld_ctor_fromStream` sets `+0x5b4 = 1`, so in the shipped game the generate
branch is unreachable. Mode 0 also asks for **paused = 1**, i.e. the game's own
edit mode — which is exactly what makes the console's `save` command legal
([dev-console.md](dev-console.md)). `save` writes `<mapdir>/init.dat`.

So the pipeline is **generate → edit → `save`**, gated by one constant. This is
not code left behind by accident: a small team used the game as its own campaign
builder rather than writing a separate editor, and closed the door on the way
out. The `1323` in the generator is a default the authoring overrode, not a
forgotten value.

### Its inputs had already stopped working

Generation dies immediately on `Fatal:Unknown textfile format! data/mitem.cfg`.
`cTextFile` accepts exactly **one** format — a file prefixed `RSA4096` — and
`data/mitem.cfg` and `data/hero.cfg` are two of only **four** files in the whole
tree with no magic at all (the others are `mvos.cfg`, which is ours, and
`servers.txt`). 4473 shipped files are `RSA4096`.

Beware the misread this doc originally made: `theocracy sux` and `mutant
technology` sit next to the magic in libmvos and look like two more formats.
They are the two **XOR keys**, periods 13 and 17
([phls-format.md](../reference/phls-format.md)). Three adjacent strings, one
format.

So the campaign builder cannot read its own inputs in the shipped build. That is
further evidence the path was closed deliberately — once nothing read those two
files at runtime, nothing forced them to keep up with the format.

### Driving it

Three knobs, all diagnostics ([diagnostics.md](../porting/diagnostics.md)):

```sh
# build a fresh campaign; lands paused in edit mode
THEOC_NEW_WORLD=1 THEOC_CONSOLE=1 ./port/build/theoc      # Prophecy → Alt+V → save

# play the result — an ordinary load, no special mode
THEOC_WORLD_FILE=$PWD/data/game/data/campaign/init.generated.dat ./port/build/theoc
```

`THEOC_NEW_WORLD` serves `RSA4096` copies of the two config files from an
anonymous temp file, so the tree stays as shipped and the plain text stays
editable. `save` is redirected to `init.generated.dat` beside the original —
`THEOC_WORLD_OUT` names another target, and pointing it at the original path is
the explicit opt-in to overwriting. Nothing is patched; the guest runs
unchanged.

### Shipped versus generated

| | shipped `init.dat` | generated |
|---|---|---|
| date | 1419/07/04 | **1323/07/04** |
| runway to the Spanish | 99 y 8 m | **195 y 8 m** |
| heroes | 12 — `hero.cfg`'s 11 **plus Jarakhi** | 11 — `hero.cfg` only |
| magic items | 20 distinct; the four editor-placed ones (9, 32, 44, 47); **no 30, 40** | 19 distinct; **30 and 40 present**; none of the four |
| guest heap | 28.6 MB | 23.6 MB |

Both dates were read out of the running game rather than computed. Note the
generated world has **no player character** — Jarakhi is a hand-edit.

**And then it was played** (2026-08-09), which is the only thing that could
settle what "generated" actually means:

- **Every AI tribe starts with fewer provinces.**
- **The starting province has different units and a different distribution.**
- **The player starts with zero slaves**, so the opening move has to be demoting
  soldiers just to feed the province. It is **not playable as shipped content**.

That is the finding. Generation produces a *scaffold* — a legal world, not a
designed one — and the designer's job was everything between it and
`data/campaign/init.dat`: the population, the balance of provinces, the player
character, and four items placed by hand. It also makes the "empty start" a
genuinely interesting basis for a new campaign, which is what the recovered
builder is now for.

## The Spanish, and what the start date costs

`SPAIN_ENTER_YEAR=1519` in `selap.txt` — historically exact for Cortés — and the
campaign mission handler turns it into an **absolute** date, not an offset from
world start:

```c
cDate_ctor_YMD(&d, SPAIN_ENTER_YEAR, 3, 7);        // 1519/03/07
FUN_081a1f30(&off, SPAIN_TIME_OFFSET_DAY / 2);     // second wave, +45 days
```

Then reinforcements every `SPAIN_TIME_OFFSET_DAY` = 90 days,
`SPAIN_UNITS_BY_PROV` = 3 waves per province, each printing the developers' own
`Incrasing spain units on realm [%d]`. `SPAIN_RND_YEAR=5` exists and is read at
four sites this pass did not follow — a player reports the arrival is slightly
randomised, which those sites presumably explain.

Because the arrival is absolute, **the start date is the campaign length**: 1419
leaves 99 years 8 months, 1323 leaves 195 years 8 months. The shipped campaign
is very close to exactly half the one the generator builds.

Whether that was a rush or a decision is not settled by anything here, and both
readings survive: 1419 → a historically exact 1519 is a clean century, while
1323 sits on the founding of Tenochtitlan. What *is* established is that the
change was made in the data, late, and the code still carries the longer
default.

**The mission deadlines are relative and were not retuned.** They are `current
date + N`, so halving the campaign did not tighten any individual mission — it
removed the slack around all of them:

```
MISSION_001/002_YEARLEFT=5      MISSION_003_MONTHLEFT=2     MISSION_007_YEARLEFT=1
MISSION_004/006/010_YEARLEFT=10
```

plus two deadlines hardcoded rather than configured — `cDate_ctor_YMD(&d, 0, 0,
15)`, **fifteen days**, gated on holding 6+ and 5+ provinces.

The console's `date <year> <month> <day>` command sets the world date directly,
so the shipped world can be played at its original length without the generator
at all: `THEOC_EDIT=1 THEOC_CONSOLE=1`, then `date 1323 7 4` and `save`.

## Open threads

- **Whether a generated world is playable is deliberately not a question here.**
  Closed 2026-08-09 by decision, not by evidence: the builder's job was always to
  hand a designer something to work on, so "not survivable as-is" is the expected
  output rather than a defect to chase. What the builder is *for* now is new
  campaigns, and the interesting version of the question is what a 195-year
  campaign should open with — a design question, not an RE one.

  One caveat to keep, because it will otherwise be misread: `THEOC_DUMP_WORLD`'s
  caste counter sits on `CreateMan_fromStream`, which is the **load** path, so a
  generated world reports `men in file: 0`. That is the instrument, not the map.

- ~~**Does the AI attack independent provinces?**~~ **Answered 2026-08-10 by
  play: it does.** The worry was that the shipped campaign hides the question —
  there the AI tribes already hold everything they will ever hold and the grey
  provinces are simply left alone, whereas a generated world leaves roughly 60%
  of the map unclaimed, so an AI that ignored independents would have opponents
  that could never grow. It was going to be the largest gap between a generated
  campaign and a designed one, larger than the missing slaves. It is not a gap:
  a generated world was played and the AI takes independent provinces. The
  standing "the AI only fights other tribes" assumption was wrong, and the
  province chooser (`FUN_0815b130`'s caller) needs no reading on this account.

  Worth keeping for whoever asks the next version of this question: the console's
  `aiprov` prints, for the province under the pointer, each tribe's capital
  distance, optimal force and actual force ([dev-console.md](dev-console.md)),
  which is how a *quantitative* answer — do they value greys as highly as an
  enemy's province? — would be got without any RE.
- ~~**`SPAIN_RND_YEAR`.**~~ **Closed 2026-08-10** — the player's recollection was
  right and the code has two mechanisms, neither of them jitter on the default
  date. First, the arrival is **two staggered waves**: `Spain0` at
  `SPAIN_ENTER_YEAR/03/07`, `Spain1` at that plus `SPAIN_TIME_OFFSET_DAY / 2`,
  each re-arming itself every `SPAIN_TIME_OFFSET_DAY` until a counter seeded from
  `SPAIN_UNITS_BY_PROV` runs out. Second, `SpainTimer_MaybeReroll` (`0x081fa6a0`,
  the other reader, called from `SimulationUpdate`) **re-rolls the whole arrival
  to a random day within `SPAIN_RND_YEAR` years once the player is down to
  `SPAIN_PROV_LIMIT` provinces they do not own** — i.e. the Spanish come early
  when you are close to winning. `0x08217de8` is the guard that makes it fire at
  most once: it tests whether the timer is still sitting on its default date.
  Full read in [missions.md](missions.md).
- **The four editor-placed items** (9, 32, 44, 47) and **Jarakhi** are the
  hand-edits that separate the shipped campaign from the generated one. Nothing
  records *why* those four items; they may simply be where a designer stood.
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
