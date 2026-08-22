# Magic items

The fifty magic items, what each one actually does, and the answer to the
question that started this: **thirteen of them ship with no flavour text, and
only three of those are unimplemented.** The missing text is a writing gap, not
cut content.

**Read off `theocracy.real` 2026-08-08.** Addresses are Ghidra space, game base
`0x08048000`. Effects are named by the game's own `selap.txt` keys; where a
claim rests on description text or on structure rather than on a read function,
it says so.

## There are exactly fifty

`Item_CreateById` (`0x0820d1f0`) is a switch over ids `1..0x32`. Each case
allocates and calls that item's own constructor — so every item is its own C++
class, and the id space is closed at 50 by the switch itself. Id `0` and
anything out of range return NULL, which is how a data file expresses "no item".

Object layout, from the constructors:

| offset | what |
|---|---|
| `+0x04` | type / slot category — always a power of two |
| `+0x08` | item id, matching the factory index |
| `+0x0c` | `cLocaleEntry*` — name (`MI_<code>`) |
| `+0x10` | `cLocaleEntry*` — description (`DESC_MI_<code>`) |
| `+0x14` | vtable |

24 bytes, except **ids 2, 8 and 50**, which take 28 and carry an extra field at
`+0x18` (id 2 initialises it to `0`; the others leave it to the base
constructor). The `+0x08` id matching the factory index in all 50 cases is a
free self-check that the extraction is right.

## Where the behaviour lives

Each item has a 16-slot vtable. Comparing all 50 side by side, only five slots
ever vary, and their defaults are:

| slot | default | role |
|---|---|---|
| 3 (`+0x0c`) | `0x081fd1c0` | equip — retag the item to a new owner, adjusting a per-type census at `0x08601108` |
| 4 (`+0x10`) | `0x081fd230` | unequip |
| 5 (`+0x14`) | `0x081fd260` | per-frame / timed tick |
| 6 (`+0x18`) | `0x081fd2b0` | damage or attack modifier |
| 7 (`+0x1c`) | `0x081fd2c0` | defence / wound modifier |

Slots 1 and 8 are per-item in all 50 (destructor pair), slot 2 is shared, and
slots 9–15 are NULL throughout.

Roles for slots 5–7 are read from what the overriding functions do, not from a
name. Slot 3/4's role is read from the default's body.

### A differing vtable slot is not behaviour

Seven of the 90 overrides do nothing but call the slot's own default and
normalise its return value. Item 1's slot-3 override is fifteen instructions
that amount to `return default(this, m) != 0`. Taking "overrides a slot" as "has
an effect" gives the wrong answer for three items, and was the first conclusion
reached here before the bodies were read. The working test is **"its only call
is the slot default, and it adds nothing around it"** — see
[re-methodology.md](../reference/re-methodology.md) §14.

## The effects are named in `selap.txt`

Item constants live at `0x084c792c..0x084c7a04`, keyed `<code>_<effect>` — the
item's own code without the `MI_` prefix. The block ends where the spell tables
begin (`Sun1_Wound`, `Moon2_ShieldPercent`, `Star4_ModifyAtt`, …), which is a
useful independent sighting of the same school names that [heroes.md](heroes.md)
derived from immunity slots.

**The key column in the table below is read from each item's override bodies,
not matched by name prefix.** Six of the fifty do not follow their own naming:
Ghost Shield (`SGH`) reads `GS_*`, Dragon Ring (`RD`) reads `DR_ATT`, Ring of
Concordance (`RPT`) reads `RT_DEF`, Symbol of Power (`MSP`) reads `MSYP_DEF`,
and — the one that actually misleads — **`SF_HP_PERCENT` belongs to Shield of
Reflection (`SFI`, id 11), not to Flame Spears (`SF`, id 31)**, whose only
constant is `SF_ATT`. A prefix match gets that pair backwards.

The vocabulary is small: `_ATT`, `_DEF`, `_HP`, `_POISON`, `_RANGE`, `_VRAD`
(view radius), `_TIME_MIN`/`_TIME_MAX`, and a few `_PERCENT` bonuses against a
specific target. Jade Bow is representative — its slot-6 override checks the
victim's man type at `+0xb3` against 2 and 0x17 and scales damage by
`BJ_JAGUAR_PERCENT` or `BJ_NATUREPRIEST_PERCENT`, otherwise defers to the
default.

Not every working item needs a constant: Mask of Protection has none, and its
description explains why — it is an immunity ("swords cannot injure its
wearer"), which is a branch, not a number.

## The fifty

`type` is the raw `+0x04` value. `placed` is which data file puts the item on
the map — `hero` = `hero.cfg`'s two item columns, `mitem` = `data/mitem.cfg`.
**The column predates the mission channel and lists data files only**; for the
25 items mission code places, and for the fourteen no code path places — which
is *not* the same as "never appear" — see "The third placement channel" and the
correction under it.

| id | code | name | type | config keys | described | placed | status |
|---|---|---|---|---|---|---|---|
| 1 | ML | Mask of the Brave | 16 | — | **no** | **nothing, anywhere** | **dead in the shipped game** |
| 2 | MH | Mask of the Snake | 16 | `MH_RANGE`, `MH_TIME_MIN/MAX` | **no** | — | works |
| 3 | MD | Mask of Death | 16 | `MD_HP` | yes | hero | works |
| 4 | ME | Mask of Eagle | 16 | `ME_VRAD` | yes | hero | works |
| 5 | MDL | Desert Lion Mask | 16 | `MDL_DEF` | yes | hero | works |
| 6 | MP | Mask of Protection | 16 | — | yes | — | works |
| 7 | MG | Mask of GOD | 16 | — | **no** | — | works |
| 8 | SPT | Moon Shield | 8 | — | yes | — | works |
| 9 | SI | Immortal Shield | 8 | `SI_HP` | yes | — | works |
| 10 | SFW | Shield of Four Winds | 8 | — | yes | hero | works |
| 11 | SFI | Shield of Reflection | 8 | `SF_HP_PERCENT` | yes | — | works |
| 12 | SLS | Shield of Lonely Star | 8 | `SLS_DEF` | **no** | — | works |
| 13 | SGH | Ghost Shield | 8 | `GS_ATT`, `GS_DEF` | yes | — | works |
| 14 | AF | Axe of Flame | 1 | `AF_ATT` | yes | mitem | works |
| 15 | AN | Axe of Nature | 1 | `AN_ATT`, `AN_HP` | yes | mitem | works |
| 16 | AS | Shark Axe | 1 | `AS_ATT`, `AS_DEF` | yes | hero+mitem | works |
| 17 | AD | Ray Axe | 1 | `AD_ATT`, `AD_DEF` | **no** | — | works |
| 18 | AW | Warrior's Axe | 1 | `AW_ATT` | **no** | — | works |
| 19 | AHC | Headcutter's Axe | 1 | `AHC_ATT`, `AHC_DEF` | yes | — | works |
| 20 | ASM | Stinning Mace | 1 | `ASM_TIME_MIN/MAX` | **no** | mitem | works |
| 21 | RD | Dragon Ring | 32 | `DR_ATT` | yes | — | works |
| 22 | RR | Lizard Ring | 32 | `RR_ATT`, `RR_HP` | yes | hero | works |
| 23 | RF | Salamander Ring | 32 | — | yes | — | works |
| 24 | RP1 | Ring Piece 1 | 128 | — | **no** | — | **inert** |
| 25 | RP2 | Ring Piece 2 | 128 | — | **no** | — | **inert** |
| 26 | RPT | Ring of Concordance | 32 | `RT_DEF` | yes | — | works |
| 27 | SL | Spears of Lightning | 2 | `SL_ATT` | yes | hero | works |
| 28 | SP | Spears of Thunder | 2 | `SP_ATT` | yes | — | works |
| 29 | SM | Moonstone Spears | 2 | `SM_TIME` | **no** | — | works |
| 30 | SJK | Jaguar Killer | 2 | `SJK_ATT` | yes | mitem | works |
| 31 | SF | Flame Spears | 2 | `SF_ATT` | yes | — | works |
| 32 | SG | Grappling Spears | 2 | `SG_ATT`, `SG_DEF` | yes | — | works |
| 33 | BJ | Jade Bow | 4 | `BJ_ATT`, `BJ_JAGUAR_PERCENT`, `BJ_NATUREPRIEST_PERCENT` | yes | — | works |
| 34 | BS | Snake Bow | 4 | `BS_POISON` | yes | — | works |
| 35 | BA | Vulture Bow | 4 | `BA_ATT` | yes | hero | works |
| 36 | SWM | Sword of Might | 1 | `SWM_ATT` | yes | hero+mitem | works |
| 37 | SWHB | Daemon Blade | 1 | `SWHB_HIT_PERCENT` | yes | — | works |
| 38 | SWAB | Axitars Blade | 1 | `SWAB_POISON` | yes | mitem | works |
| 39 | SWMB | Stark Blade | 1 | `SWMB_ATT`, `SWMB_DEF` | yes | hero | works |
| 40 | SWFB | Falcon Blade | 1 | `SWFB_VRAD` | yes | mitem | works |
| 41 | SWNB | Night Blade | 1 | `SWNB_ATT` | **no** | — | works |
| 42 | SWDB | Blade of Death | 1 | `SWDB_ATT`, `SWDB_DEF` | yes | hero | works |
| 43 | ERL | Earring of Life | 32 | `ERL_HP` | yes | — | works |
| 44 | ERB | Earring of Balance | 32 | — | yes | — | works |
| 45 | ERS | Earring of Shielding | 32 | `ERS_ATT`, `ERS_DEF` | yes | — | works |
| 46 | MEA | Warp Medallion | 64 | — | yes | — | works |
| 47 | MSP | Symbol of Power | 128 | `MSYP_DEF` | yes | — | works |
| 48 | MSM | Symbol of Magic | 128 | — | yes | — | works |
| 49 | MPI | Polearm of Ice | 128 | `MPI_ATT` | **no** | — | works |
| 50 | MBH | Bone Horn | 128 | `MBH_RANGE`, `MBH_TIME` | **no** | — | works |

The `type` values group by item family: `1` melee weapons (axes and swords), `2`
spears, `4` bows, `8` shields, `16` masks, `32` rings and earrings, `64` and
`128` the oddments. Three warning strings in the locale go with it — "Your man
cannot use this magic item", "Your man is using **another magic item of this
type**", "No more room for another magic item".

**The checker was read 2026-08-10** — `cMan_TryEquipItemSlot` (`0x080a81f0`),
called by `cMan_GiveItem` once the item is in a slot — and it settles
bitmask-vs-category: **it is a bitmask**, and the ambiguity was real because the
field is used *both* ways in the same function.

```c
u32 allowed = item->type;                        // +0x04
u32 canUse  = man->vtable[0x24](man) & 0xffff;   // per-man-class capability mask
if ((canUse & allowed) == 0)
    refuse("Your man cannot use this magic item");
else {
    other = man->items[1 - slot];
    if (other && other->equipped && other->type != 0x80 && other->type != 0x20
        && other->type == item->type)
        refuse("Your man is using another magic item of this type");
    else
        item->vtable[0x0c](item, man);           // equip
}
```

So the **carry** test is a genuine bitwise AND against a mask the man's class
supplies, while the **duplicate** test compares the field for equality — which
is what makes it read like a category id. Both work because every shipped value
is a single bit.

The duplicate rule has an exemption nothing else records: **types `0x80` and
`0x20` are excluded from it**, so a man may hold two rings/earrings (`0x20`) or
two of the `0x80` oddments, but not two shields. The third warning string is
`cMan_GiveItem`'s own, for the two-slot limit, and is checked before either of
these.

### Who can carry what

**Read 2026-08-10.** The other half of that AND — man vtable slot `+0x24` — is
`cMan::GetItemCarryMask`, and every implementation in the image is a single
`return <constant>`. So the entire carrier table is sixteen numbers, recovered
by reading slot `+0x24` out of each man-class vtable and the constant out of
each function it points at:

| mask | classes | can carry |
|---|---|---|
| `0xf9` | `cMan_Swordsman`, `cMan_Swordsman_Hero` | melee, shield, mask, ring, oddments |
| `0xfa` | `cMan_Spearman`, `cMan_Spearman_Hero` | spear, shield, mask, ring, oddments |
| `0xfc` | `cMan_Archer_Hero`, `cMan_BigVampire` | bow, shield, mask, ring, oddments |
| `0xf4` | `cMan_Archer` | bow, mask, ring, oddments — **no shield** |
| `0xf8` | `cMan_Comm1` | shield, mask, ring, oddments — no weapon |
| `0xf0` | `cMan_JudasPriest` 1–5, `cMan_Governor` | mask, ring, oddments |
| `0xe0` | `cMan_MoJaguar` | ring, oddments |
| `0xb0` | `cMan_Spy` | mask, ring, `0x80` oddments — **not `0x40`** |
| `0x00` | the other 27 | nothing |

**The default is zero**, so carrying is opt-in and most of the roster is
excluded: every civilian (farmer, miner, woodcutter, builder, trader, slave,
lama driver, `cMan_Kezmuves`), and also `cMan_Vampire`, `cMan_Jaguar`,
`cMan_Lama`, `cMan_Shadow`, `cMan_Stonewarrior`, `cMan_Cortes` and
`cMan_Dragonkiller`.

Three things fall out that the item table alone could not say:

- **No man carries two weapon families.** Swordsman gets `1`, spearman `2`,
  archer `4`, and nobody gets a second — the exclusivity is in the carrier, not
  in the item.
- **The archer is the only class whose hero variant differs.** Swordsman and
  spearman have identical masks to their hero versions; `cMan_Archer` is `0xf4`
  and `cMan_Archer_Hero` is `0xfc`, so **only a hero archer may carry a shield**.
- **Every item type has at least one carrier**, so no item is unequippable by
  construction — which, with the `+0x18` finding above, means the inert items are
  inert for authoring reasons and never for lack of a hand to hold them.

`cMan_BigVampire` sharing the hero-archer mask is the one entry that reads as a
design oddity rather than a rule; recorded as observed.

## The thirteen without flavour text

Thirteen items have a description equal to their own name: **1, 2, 7, 12, 17,
18, 20, 24, 25, 29, 41, 49, 50**. Every other item runs 122–415 characters.

This is **not a localisation gap**. All six shipped languages are short for
exactly the same thirteen keys and full for all the others — the lengths differ
only because each language translated the *name* into the description slot.
Whatever produced these files had no source text to work from in any language.

Of the thirteen, **ten are fully implemented** — they have real vtable overrides
and, in most cases, their own balance constants. Ray Axe reads `AD_ATT` and
`AD_DEF`; Night Blade reads `SWNB_ATT`; Bone Horn has a working timed effect
with a counter capped by `MBH_TIME`. A player who picks these up gets exactly
what the numbers say; nobody wrote the sentence.

Three are genuinely inert:

- **Ring Piece 1 (24)** and **Ring Piece 2 (25)** — both override only slots 3
  and 4, and both overrides just forward to the default. No constants. Their
  names, their shared type `128` (not `32`, the ring type), and the existence of
  a fully-implemented **Ring of Concordance (26)** make these read as quest
  tokens rather than equipment, in which case doing nothing when carried is
  correct behaviour and not a defect. The combining step has not been found —
  see Open threads.
- **Mask of the Brave (1)** — the real oddity. Masks 2 through 7 all have
  effects; this one has no constants, no working override, and no description,
  and it is the first entry in the table. It is the item equivalent of Umochi
  ([heroes.md](heroes.md)).

## The two placement files

`data/mitem.cfg` is plaintext, loaded at `0x081fb5b0` immediately before
`data/hero.cfg`, parsed at `0x08214ef0` into **13-byte records**:

| column | offset | meaning |
|---|---|---|
| 0 | `+0x08` u8 | item id |
| 1 | `+0x00` i32 | map x |
| 2 | `+0x04` i32 | map y |
| 3 | `+0x09` i32 | province index |

The consumer (`0x08214e30`) creates the item, then creates a man in that
province and hands the item over via virtual `+0xa0`. Its diagnostic —
`"Couldn't create man, becouse prov_id to big or mitem NULL"` — is the
developers' own, typo included. Eight items are placed this way; `hero.cfg`
places up to two more per hero.

Between them the two files place 17 of the 50 items. **That is not evidence the
other 33 are unreachable** — and it turned out not to be: mission code places 25
more, including the Ring pieces. See "The third placement channel" below.

## The third placement channel — mission code

**Read 2026-08-09; full write-up in [missions.md](missions.md).** The claim
above that `Item_CreateById` is "the only way an item comes into existence" is
**wrong, and was wrong in the confident direction**: mission code calls the
fifty per-item constructors *directly*, bypassing the factory switch. Xref'ing
the constructors rather than the factory finds 25 items with a non-factory
caller ([re-methodology.md](../reference/re-methodology.md) §15).

That closes both of this doc's headline open questions:

- **The Ring Pieces do combine.** `cMission_TwoRings_Start` (`0x0821c5e0`) drops
  ids 24 and 25 on the map. When one man carrying both walks into the mission's
  `cBld_Ring` building, its "man entered" virtual (`+0xd4`,
  `cBld_Ring_OnManEntered` at `0x08295d10`) destroys both halves, constructs
  **id 26 Ring of Concordance** and gives it to that same man — the Ring
  *replaces* the pieces on their carrier. Either carry order works. So ids 24
  and 25 are inert *as items* and load-bearing *as quest tokens*, exactly as
  this doc guessed, and the mechanism is in the building rather than in the
  items — which is why nothing in their own vtables showed it.
- **Mask of the Brave (1) is dead in the shipped game.** With the construction
  surface now complete, nothing creates it: not `mitem.cfg`, not `hero.cfg`, not
  any mission, and `.man` files have no item column. Only the developer console
  can produce one.

But it is **not uniquely dead**. The union of all three channels is 36 items;
**fourteen are created by no code path** — ids 1, 2, 7, 9, 12, 17, 18, 29, 32,
41, 44, 47, 49, 50. Item 1 is merely the only one of the fourteen that is *also*
inert, which is what made it look singular from the code side.

And the two gaps coincide. **Ten of the thirteen undescribed items are among the
fourteen** (1, 2, 7, 12, 17, 18, 29, 41, 49, 50); the only described-less ones
any code path places are 20 and the two Ring Pieces.

### Correction — there is a fourth channel, and it is not code

**Same day.** The paragraph above first read "created by nothing… no player will
ever hold one". Withdrawn: the audit was exhaustive over *code* and there is a
data channel it cannot see.

**Every `init.dat` in the tree — the campaign's and all eight scenarios' — is a
`theosg42` savegame** ([save-format.md](save-format.md)), so the starting world
of every map is *loaded*, not placed. Items in it come up through the **stream
constructor at `0x0820dbb0`**, which reads an id byte and calls
`Item_CreateById` — meaning a shipped world file can materialise **any of the
fifty**, including Mask of the Brave.

So the honest state of the question:

- "**No code creates item 1**" — established.
- "**Item 1 is dead in the shipped game**" — *not* established, and must not be
  quoted from this doc until `init.dat` is parsed.
- The ten-of-thirteen correlation stands as stated (it is a claim about code
  paths), but the inference from it to player experience does not.

The parallel correction on the hero side is sharper still, and is what surfaced
this: Jarakhi is the campaign's **player character** and is assigned by no code
path either — because he ships in the world state ([heroes.md](heroes.md),
[missions.md](missions.md)).

### …and the fourth channel has now been read — the withdrawal is lifted

**2026-08-09, same day again.** All nine world files were read by the game's own
loader ([starting-world.md](starting-world.md)), so the fourth channel is no
longer an unknown:

- **Mask of the Brave (1) is in none of the nine.** All four channels are now
  checked, and the strong claim is restored: **item 1 is dead in the shipped
  game**, reachable only from the developer console. Quote it again.
- **Four of the fourteen do ship after all** — 9, 32, 44 and 47 are in
  `data/campaign/init.dat`, placed with an editor rather than by any code. So
  "created by no code path" was the right wording and the caution was warranted:
  it was not, for those four, the same as absent.
- **Ten of the fourteen are genuinely in nothing**: 1, 2, 7, 12, 17, 18, 29, 41,
  49, 50. Nine of them work perfectly and no shipped content hands them out.
- The ten-of-thirteen correlation is untouched, and now says something about the
  player's experience as well as about code paths: the items nobody wrote lore
  for are overwhelmingly the items nobody placed.

The `placed by` column in the table above is still a code-path column. For what
a player actually finds on the map, read the census in
[starting-world.md](starting-world.md).

## Open threads

- ~~**The `+0x04` type field's exact semantics**~~ — **closed 2026-08-10**, above:
  a bitmask for the carry test and an equality key for the duplicate test, with
  `0x80` and `0x20` exempt from the latter.
- **The `+0x18` field on ids 2, 8 and 50** — **mechanism answered, initialisation
  is a defect.** (Note the collision: this is the *object* field at `+0x18`, not
  the *vtable* slot at `+0x18` in the table above.) The three 28-byte items are
  exactly the three that need to remember something between calls. Moon Shield
  (8): its slot-7 override is `*(byte*)(this+0x18) ^= 1`, returning zero damage
  when the bit is set — it blocks literally **every other** sword blow, which is
  what its description claims. Bone Horn (50) uses a counter capped by `MBH_TIME`.
  Id 2's use (`MH_TIME_MIN`/`MAX`) is still unread.

  **But only id 2 initialises it.** Its constructor (`0x081fd3c0`) writes
  `*(byte*)(this+0x18) = 0`; ids 8 (`0x081fd920`) and 50 (`0x081ff6d0`) write
  nothing there, the base constructor (`0x081fcfb0`) stops at `+0x12` because the
  base object is only `0x18` bytes, and all three use the **default** equip and
  unequip (`0x081fd1c0`/`0x081fd230`, confirmed from their vtables) which never
  touch it. So Moon Shield and Bone Horn read a byte `operator new` left as heap
  garbage. For Moon Shield the effect is bounded — the XOR still alternates, so
  only the *phase* is random (whether it blocks the 1st or the 2nd blow). For
  Bone Horn a garbage starting counter against `MBH_TIME` is potentially visible,
  and pinning that needs its slot-5 body read.
- **Slots 5–7 roles** are read from a sample of overriding bodies, not from all
  90. A full pass would firm up the slot table.
- **Items 30 and 40** are placed by `mitem.cfg` and are in no world file — see
  [starting-world.md](starting-world.md), "Open threads".

## Cross-references

- [heroes.md](heroes.md) — the item factory's other caller, and the same
  unfinished-content pattern in the hero roster.
- [phls-format.md](../reference/phls-format.md) — the `.sdb` locale format used
  throughout here.
- [re-methodology.md](../reference/re-methodology.md) §14 — the vtable-override
  trap this task walked into.
