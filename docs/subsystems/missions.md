# Missions

The scripted layer that sits on top of the simulation: twenty `cMission_*`
classes that spawn men from their own config files, watch for a condition, and
hand out heroes and magic items when it is met. This is the wall that
[heroes.md](heroes.md) and [magic-items.md](magic-items.md) both hit, and it
answers all three of their questions.

**Read off `theocracy.real` 2026-08-09.** Addresses are Ghidra space, game base
`0x08048000`. Findings are mirrored into the Ghidra DB as renames and
decompiler comments.

## The trap that hid all of this

`magic-items.md` said `Item_CreateById` (`0x0820d1f0`) was "the only way an item
comes into existence", and the obvious first move — xref it — produces eight
call sites and not one mission among them:

| site | caller |
|---|---|
| `0x08214e62` | `MitemCfg_PlaceItems` — `data/mitem.cfg` |
| `0x082152a5`, `0x082152cb` | `HeroCfg_PlaceHeroes` — the two item columns of `data/hero.cfg` |
| `0x0820dbd0` | item deserialisation from a save stream (`0x0820dbb0`) |
| `0x081eefbe`, `0x081f1c89`, `0x081f1ca8` | the developer console's `mitem` and `hero` commands (`0x081eed90`) |
| `0x081e4071` | the plain-key cheat dispatcher (`FUN_081e2330`, see [dev-console.md](dev-console.md)) |

The conclusion "no mission places an item" would have been wrong, and wrong in
the confident direction. **Mission code calls the fifty per-item constructors
directly** — `new(0x18)` followed by `cMagicItem_<X>_ctor` — bypassing the
factory switch entirely. The question only opens up when you xref the
*constructors* instead of the factory: 25 of the 50 have a caller that is not
`Item_CreateById`.

Generalised into [re-methodology.md](../reference/re-methodology.md): a factory
function is evidence about the paths that *use* the factory, never about the
full construction surface of the type. Check the constructors.

## The twenty classes

Twenty RTTI names, all deriving from one base whose type-info getter is
`0x08210180` — that getter has exactly 20 callers, which is what fixes the count
at twenty rather than at "twenty strings we found".

Twelve are the *named* missions, compiled into one translation unit, with a
contiguous run of 20-slot (`0x50`-byte) vtables. `ctor` is the address of the
instruction that installs the vtable.

| vtable | ctor installs at | class |
|---|---|---|
| `0x083a8720` | `0x0821ec15` | `cMission_WallChecker` |
| `0x083a8780` | `0x0821d97a` | `cMission_TheWall` |
| `0x083a87e0` | `0x0821d005` | `cMission_Josda` |
| `0x083a8840` | `0x0821cdc5` | `cMission_Josda_Pre` |
| `0x083a88a0` | `0x0821c575` | `cMission_TwoRings` |
| `0x083a8900` | `0x0821bd87` | `cMission_HeavyArmory` |
| `0x083a8960` | `0x0821b2f5` | `cMission_Dragon` |
| `0x083a89c0` | `0x0821a58a` | `cMission_Vampire` |
| `0x083a8a20` | `0x08219d65` | `cMission_Scroll_lost` |
| `0x083a8a80` | `0x082190b5` | `cMission_Scroll` |
| `0x083a8ae0` | `0x082186f7` | `cMission_VillageOfJaguar` |
| `0x083a8b40` | `0x08217f07` | `cMission_MountainVillage` |

The other eight are the campaign-scenario missions — `S1_0`, `S2_0`, `S4_0`,
`S5_0`, `S5_1`, `S6_0`, `S6_1`, `S8_0` — each in its own translation unit with
its vtable elsewhere (`cMission_S6_0` is at `0x083bc6a0`). They share the base
layout: `cMission_S6_0::Check` sits at slot `+0x18` exactly as the named ones do.

### Vtable layout

Derived by diffing all twelve named vtables slot by slot and confirming each
against a decompiled body.

| slot | role | evidence |
|---|---|---|
| `+0x00` | delta, always `0` | GNU v2 |
| `+0x04` | `__rtti_si` getter | 12 distinct, one per class |
| `+0x0c`, `+0x10` | base `cMission` (`0x0820e570` / `0x0820e610`) | shared by 11 of 12 |
| `+0x14` | **`Start(province)`** | stores the province at `this+0x24`; `cMission_TwoRings_Start`, `cMission_TheWall_Start`, `cMission_S4_0_Start` |
| `+0x18` | **`Check()` → state** | returns `this+0x2c`; `cMission_TwoRings_Check` |
| `+0x1c` | **`Activate()`** | `this` only; `cMission_Josda_Activate` loads its man file here, `cMission_TwoRings`' does nothing but set two flags |
| `+0x2c` | **`Finish()`** | branches on the state byte; five of the six hero rewards live here |

Both `+0x14` and `+0x1c` spawn men, depending on the mission — four of the six
man files are loaded from `Activate` and two from `Start`. That is a real
difference between the classes, not a misread slot: the same two functions were
read for TwoRings (`+0x1c` at `0x0821c5c0` is two stores) and for Josda (`+0x1c`
at `0x0821d050` loads `misi010.man`).

### Object fields

| offset | meaning |
|---|---|
| `+0x24` | the `cProvince*` the mission runs in |
| `+0x2c` | **state**: `0` running, `1` complete, `2` failed |
| `+0x2d` | active |
| `+0x2e` | started |
| `+0x38` | already finished — `Finish` returns immediately if set |
| `+0x3c`… | per-mission: the watched building, the placed items, REF-node lists |

## The man config files

`MissionCfg_Parse` (`0x08214ac0`) reads a `.man` file; `MissionCfg_PlaceMen`
(`0x082144c0`) parses and then spawns. **The files are `RSA4096`-XOR encrypted**
like `selap.txt` — decrypt with `tools/theocracy_crypt.py` before grepping, or
you will get a confident false negative
([re-methodology.md](../reference/re-methodology.md) §13).

Five whitespace-separated integers per line, into a 14-byte record:

| column | record offset | meaning |
|---|---|---|
| 0 | `+0x08` u8 | man type |
| 1 | `+0x0d` u8 | tribe |
| 2 | `+0x00` i32 | map x |
| 3 | `+0x04` i32 | map y |
| 4 | `+0x09` i32 | count |

`MissionCfg_PlaceMen` loops the count, resolving the position through province
virtual `+0x1c` and creating each man through province virtual `+0xe4`. Its
diagnostics are the developers' own: `"Add man[ TYPE:%d TRIBE:%d ]"`,
`"Failed to place man becouse VOID_POS"`.

Six files ship, and the number in the filename is the mission number:

| file | loaded by | at |
|---|---|---|
| `misi003.man` | `cMission_Scroll` | `+0x1c` |
| `misi004.man` | `cMission_Scroll_lost` | `+0x1c` |
| `misi005.man` | `cMission_Vampire` | `+0x1c` |
| `misi008.man` | `cMission_TwoRings` | `+0x14` |
| `misi010.man` | `cMission_Josda` | `+0x1c` |
| `misi011.man` | `cMission_TheWall` | `+0x14` |

The campaign missions use the same machinery against per-scenario files —
`cMission_S6_0` loads `data/scenario/scn6/mancfg/command_unit.man` — which is
why `data/scenario/scn*/mancfg/` exists and holds the same five-column format.
**No `.man` file has an item column**, which matters below.

## Province virtuals the missions drive

Read from their call sites rather than from the province class, so the argument
lists are certain and the bodies are not.

| slot | signature | what |
|---|---|---|
| `+0x1c` | `(out, prov, &xy, flags)` | resolve a raw map `(x,y)` pair into the engine's position form |
| `+0xd8` | `(prov, pos, item)` | drop a magic item on the ground |
| `+0xe0` | `(prov)` | a mode predicate — selects between the two placement paths in `MissionCfg_PlaceMen` |
| `+0xe4` | `(prov, pos, manType, tribe)` → `cMan*` | create a man |

`+0xe4` is the same virtual `HeroCfg_PlaceHeroes` uses, and it is handed the
first eight bytes of the `hero.cfg` record as its position — which settles a
leftover from [heroes.md](heroes.md): **`hero.cfg` columns 3 and 4 are the map
position**, and they are `0` in every shipped row because those heroes are
positioned by the province rather than by the file.

## Missions place heroes

`cHero_SetHeroId` (`0x080b23d0`) is a virtual at slot `+0x58` of the hero
vtable (`0x0831af20`, installed at `0x080b229b`). The base `cMan` vtable
(`0x0831ae00`) has a different function in that slot, so the slot alone is not
enough — every candidate was checked for a two-argument call.

It is **not** the only writer of the hero id, though this doc first said so:
`cHero`'s stream constructor writes `+0x27c` directly from save data. See "The
third channel" below. It is the only writer that *chooses* an id.

Across the whole binary there are **exactly five call sites that pass a constant
hero id**, plus the two variable ones (`hero.cfg` at `0x08215293`, the dev
console at `0x081f1c7d`):

| site | mission | hook | hero | man type | items given |
|---|---|---|---|---|---|
| `0x08218d05` | `cMission_VillageOfJaguar` | `Finish` | **10 Garkuna** | `0x21` swordsman | 15 |
| `0x0821988a` | `cMission_Scroll` | `Finish` | **3 Toomoo** | `0x22` spearman | 21, 11 |
| `0x0821ca52` | `cMission_TwoRings` | `Finish` | **5 Kukurbuki** | `0x22` spearman | 45, 28 |
| `0x0821d560` | `cMission_Josda` | `Finish` | **8 Akrisi** | `0x22` spearman | 31, 13 |
| `0x0821dc0d` | `cMission_TheWall` | `Start` | **17 Skalaki** | `0x23` archer | 34 |

So five of the eight heroes `hero.cfg` leaves out are mission rewards. Skalaki
is the odd one: he is handed over when The Wall *begins*, not when it ends.

### The three no code path assigns

`hero.cfg` places 11, missions place 5 — sixteen of the nineteen. **Umochi (9),
Jarakhi (11) and Tlechlal (19) are assigned by no code path**: no data file, no
mission, nothing outside the developer console.

**That is not the same as "never appear", and for Jarakhi it is definitely not**
— see "The third channel" below, which is the correction that matters most in
this doc. He is the campaign's player character and ships in the world state.

For Umochi the code-side silence still agrees with what [heroes.md](heroes.md)
argued from his placeholder description, and he is the one of the three whose
absence a stream-loaded hero would *not* explain away — his description is the
string `Umochi`, which no amount of map data fixes.

### One hero placed without an identity

Six sites print `"Failed to place hero to (%d,%d)"`. Five of them follow the
placement with `SetHeroId`. The sixth — `cMission_Scroll_lost_Finish`
(`0x0821a110`) — places a man of type `0x23` at `(0xd9,0xb8)`, gives him item 3,
and never assigns an id, so he keeps the `0` the `cHero` constructor wrote.
Recorded as observed; whether that is deliberate (a nameless survivor for the
*lost*-scroll branch) or an omission is not decidable from the code.

## Missions place items

Twenty-five of the fifty item constructors have a caller outside the factory.

| mission | items placed or given |
|---|---|
| `cMission_Dragon` | 6, 23, 40, 43, 46 |
| `cMission_TwoRings` | 19, 24, 25, 28, 38, 45 |
| `cMission_Vampire` | 8, 37, 48 |
| `cMission_Scroll` | 11, 21 |
| `cMission_Scroll_lost` | 3 |
| `cMission_VillageOfJaguar` | 15, 30 |
| `cMission_Josda` | 13, 31 |
| `cMission_TheWall` | 14, 34 |
| `cMission_S6_0` | 42 |
| `cBld_Ring` | 26 |

## The Two Rings, end to end

The one mission read all the way through, because it answers the question
`magic-items.md` left open. Every step is code, not inference.

**1 — `cMission_TwoRings_Start` (`0x0821c5e0`, slot `+0x14`).** Loads
`misi008.man`. Walks the province building list for the one whose flags have bit
`2` (capture) set and stores it at `this+0x3c`, dying with
`Fatal("!!! Nincs gyurus haz !!!")` — *"there is no ring house"* — if there
isn't one. Then constructs **Ring Piece 1** (`0x081fdf20`, id 24) and drops it
at map `(0x8f, 0xca)`, and **Ring Piece 2** (`0x081fdf90`, id 25) at
`(0xc7, 0x162)`, through province virtual `+0xd8`. Both constructors are called
directly; neither goes through `Item_CreateById`.

**2 — a man picks both up.** A `cMan` carries at most two magic items, in
pointers at `+0xd0` and `+0xd4` with the count at `+0xd8`.

**3 — he walks into the ring house.** Building virtual `+0xd4` is a *"a man
reached this building"* hook whose default (`cBuilding_OnManEntered_default`,
`0x081be2f0`) is an empty body. Across every building vtable in the image
**exactly two classes override it**: `cBld_Ring` and `cBld_Monsta`. The call
site is in the man's movement step (`0x0809caad`, in `FUN_0809c890`), reached
when the man's movement state is `2`:

```c
(**(code **)(*(int *)(building + 8) + 0xd4))(building, man);
```

**4 — `cBld_Ring_OnManEntered` (`0x08295d10`) does the combining:**

```c
if ((slot0 == 0x18 && slot1 == 0x19) || (slot0 == 0x19 && slot1 == 0x18)) {
    cMan_DestroyItem(man, 0);          // twice on slot 0 — slot 1 shifts down
    cMan_DestroyItem(man, 0);
    item = new(0x18); cMagicItem_RingOfConcordance_ctor(item);   // id 26
    cMan_GiveItem(man, item);
    building[0x178] = 1;
}
```

Either order works. The two halves are destroyed, not merely unequipped, and the
whole Ring lands on **the same man** who carried them.

**5 — `cMission_TwoRings_Check` (`0x0821c7d0`, slot `+0x18`)** polls
`building[0x178]` through `cBld_Ring_IsSolved` (`0x08295dd0`) and sets state `1`.
It sets state `2` if no living man is left in the province —
`"Nincs elo ember[%d] a provincian (mission fail)."`

**6 — `cMission_TwoRings_Finish` (`0x0821c920`, slot `+0x2c`)** plays
`tworings.mpg`, places **Kukurbuki (hero 5)** with items 45 and 28, and drops
items 19 and 38 at `(0x8e, 0xa2)`.

`cBld_Monsta` is the same shape one step simpler: its hook (`0x08294120`) scans
the man's item slots for id `0x15` (21, the Dragon Ring) and sets the same
`+0x178` flag, with no transformation.

## Fourteen items no *code* path creates — and the channel that is not code

**Corrected 2026-08-09, the same day, after this section first claimed those
fourteen were "created by nothing". They are not. See "The third channel" below
before using any number here.**

Placed by `mitem.cfg` (8), `hero.cfg` (12) or mission code (25), the union is
**36 items**. The other **fourteen are created by no code path in the image**:
ids **1, 2, 7, 9, 12, 17, 18, 29, 32, 41, 44, 47, 49, 50**.

That statement is exhaustive over *code*, and the exhaustiveness is real:
`Item_CreateById`'s four callers plus 25 direct constructor calls, and no
indirect call through a stored function pointer — scanning the image for the
address of `Item_CreateById` and of each of those fourteen constructors finds
exactly one occurrence each, all inside `.eh_frame`
(`0x084e3be8`–`0x08597400`), i.e. unwind FDEs and not a dispatch table.

The two gaps line up neatly, and this part survives: of the thirteen items whose
description is just their own name, **ten are among the fourteen** (1, 2, 7, 12,
17, 18, 29, 41, 49, 50). The only described-less items any code path places are
20 (`mitem.cfg`) and the two Ring Pieces.

## The third channel — the shipped world is a savegame

**Every `init.dat` in the data tree is a `theosg42` save file**: the campaign's
(`data/campaign/init.dat`, 550 831 bytes) and all eight scenarios'
(`data/scenario/scn*/init.dat`). The magic is at offset `0x40` and the first
`0x40` bytes are exactly the uninitialised-stack header
[save-format.md](save-format.md) documents — which had already recorded that the
console's `save` command writes `init.dat`. The connection was there to be made
and this doc's first version did not make it.

So **the starting state of every map is loaded, not placed**, and objects in it
come up through *stream constructors* rather than through any of the three
channels above:

- **`cHero`'s stream constructor is `0x080b22c0`.** It runs the base `cMan`
  stream ctor, installs the hero vtable, and then reads **one byte straight into
  `+0x27c`** — the hero id, never touching `cHero_SetHeroId`.
- **Items likewise**: `0x0820dbb0` reads an id byte and calls `Item_CreateById`,
  so a stream can materialise any of the fifty.

And this is why neither showed up in an xref sweep. The per-man-type **caste
properties** struct carries three function pointers, and the stream loader is
one of them — `FUN_08254570` registers `_DAT_0866d828 = FUN_082543b0` for man
type `0x21`. Construction goes through that table, so it is an indirect call
with no rel32 anywhere. Exactly the shape of the factory-vs-constructor trap one
level up: the same sweep that was exhaustive over direct calls was blind to a
registered pointer ([re-methodology.md](../reference/re-methodology.md) §15).

### What this does to the claims above

Every "created by nothing" / "placed by nothing" statement in this doc must be
read as **"assigned by no code path"**. The world-state files have not been
parsed, and they are a live channel for both heroes and items:

- **Mask of the Brave (1)** may be sitting in a province in `init.dat`. "Dead in
  the shipped game" is *not* established; "no code creates it" is.
- **Jarakhi (11)** is the concrete counter-example, from the game's own lore:
  he is the **player character** of the campaign, so he is neither spawned nor
  rewarded — he ships in the starting world state, which is precisely what a
  code-only audit cannot see. Supporting evidence in the binary: his portrait is
  preloaded by a caste registration (`0x08254def`, in the same block as heroes
  1–8), which the game would not do for content that never appears. Not yet
  verified byte-wise in `init.dat` — see `todo.md`.
- The **ten undescribed-and-uncreated** items remain the interesting
  correlation, but "no player will ever hold one" is withdrawn until `init.dat`
  is read.

`hero.cfg`'s load condition gains a likely meaning here too: it runs only when
`*(int *)(g_GameSession + 0x4c) == 0` ([heroes.md](heroes.md)), which now reads
as "a new game, rather than a loaded world" — untested, and stated as a
reading.

## Open threads

- **`cMission_HeavyArmory`, `cMission_MountainVillage`, `cMission_Josda_Pre`,
  `cMission_WallChecker`** are named and slotted but unread. None places a hero
  or an item, which is why they were not needed here.
- **The eight campaign missions (`cMission_S*_*`)** are read only far enough to
  establish that they share the base vtable and the `.man` machinery.
  `cMission_S4_0_Start` looks its commander and hero up by man type through
  `FUN_08211e10` rather than creating them — that lookup helper, and the
  `flag:%d` REF-node scanning around it, is the natural next target.
- **What starts a mission**, and how the twelve named ones are bound to
  provinces and to the campaign, is not read here. Only the lifecycle after
  `Start` is.
- **Province virtual `+0xe0`**, the predicate that picks between the two
  placement paths, is unread — named from its use, not its body.
- **`cMan_Comm1` writes `26` to `+0x27c`** in its constructor (`0x08245920`) and
  `FUN_08246150` copies a man *type* (`+0xb3`) into the same byte. Either
  `+0x27c` is a general subtype byte that `cHero` uses for the hero id, or one of
  the two readings is wrong. It does not affect anything above — the five
  constant writes all go through `cHero_SetHeroId` — but
  [heroes.md](heroes.md)'s "the hero id is the byte at `+0x27c`" should be read
  as class-scoped until this is settled.

## Cross-references

- [heroes.md](heroes.md) — the roster, and the five ids this doc places.
- [magic-items.md](magic-items.md) — the fifty items and their effects.
- [dev-console.md](dev-console.md) — the `mitem`/`hero` commands and the cheat
  dispatcher, which between them are the last two callers of `Item_CreateById`.
- [phls-format.md](../reference/phls-format.md) — the `RSA4096` XOR the `.man`
  files use.
- [re-methodology.md](../reference/re-methodology.md) §13 (grepping ciphertext)
  and the new factory-vs-constructor note.
