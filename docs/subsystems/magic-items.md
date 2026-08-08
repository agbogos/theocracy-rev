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
that amount to `return default(this, m) != 0`. Taking "overrides a slot" as
"has an effect" gives the wrong answer for three items, and was the first
conclusion reached here before the bodies were read. The working test is
**"its only call is the slot default, and it adds nothing around it"** — see
[re-methodology.md](../reference/re-methodology.md) §14.

## The effects are named in `selap.txt`

Item constants live at `0x084c792c..0x084c7a04`, keyed `<code>_<effect>` — the
item's own code without the `MI_` prefix. The block ends where the spell tables
begin (`Sun1_Wound`, `Moon2_ShieldPercent`, `Star4_ModifyAtt`, …), which is a
useful independent sighting of the same school names that
[heroes.md](heroes.md) derived from immunity slots.

The vocabulary is small: `_ATT`, `_DEF`, `_HP`, `_POISON`, `_RANGE`, `_VRAD`
(view radius), `_TIME_MIN`/`_TIME_MAX`, and a few `_PERCENT` bonuses against a
specific target. Jade Bow is representative — its slot-6 override checks the
victim's man type at `+0xb3` against 2 and 0x17 and scales damage by
`BJ_JAGUAR_PERCENT` or `BJ_NATUREPRIEST_PERCENT`, otherwise defers to the
default.

Not every working item needs a constant: Mask of Protection has none, and its
description explains why — it is an immunity ("swords cannot injure its wearer"),
which is a branch, not a number.

## The fifty

`type` is the raw `+0x04` value. `placed` is which data file puts the item on
the map — `hero` = `hero.cfg`'s two item columns, `mitem` = `data/mitem.cfg`.

| id | code | name | type | config keys | described | placed | status |
|---|---|---|---|---|---|---|---|
| 1 | ML | Mask of the Brave | 16 | — | **no** | — | **inert** |
| 2 | MH | Mask of the Snake | 16 | `MH_RANGE`, `MH_TIME_MIN/MAX` | **no** | — | works |
| 3 | MD | Mask of Death | 16 | `MD_HP` | yes | hero | works |
| 4 | ME | Mask of Eagle | 16 | `ME_VRAD` | yes | hero | works |
| 5 | MDL | Desert Lion Mask | 16 | `MDL_DEF` | yes | hero | works |
| 6 | MP | Mask of Protection | 16 | — | yes | — | works |
| 7 | MG | Mask of GOD | 16 | — | **no** | — | works |
| 8 | SPT | Moon Shield | 8 | — | yes | — | works |
| 9 | SI | Immortal Shield | 8 | `SI_HP` | yes | — | works |
| 10 | SFW | Shield of Four Winds | 8 | — | yes | hero | works |
| 11 | SFI | Shield of Reflection | 8 | — | yes | — | works |
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
| 31 | SF | Flame Spears | 2 | `SF_ATT`, `SF_HP_PERCENT` | yes | — | works |
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
`128` the oddments. Given three warning strings in the locale — "Your man cannot
use this magic item", "Your man is using **another magic item of this type**",
"No more room for another magic item" — this is the equip-restriction field. The
checker itself has not been read, so treat the exact semantics (bitmask of
allowed carriers vs. category id) as unsettled.

## The thirteen without flavour text

Thirteen items have a description equal to their own name: **1, 2, 7, 12, 17, 18,
20, 24, 25, 29, 41, 49, 50**. Every other item runs 122–415 characters.

This is **not a localisation gap**. All six shipped languages are short for
exactly the same thirteen keys and full for all the others — the lengths differ
only because each language translated the *name* into the description slot.
Whatever produced these files had no source text to work from in any language.

Of the thirteen, **ten are fully implemented** — they have real vtable overrides
and, in most cases, their own balance constants. Ray Axe reads `AD_ATT` and
`AD_DEF`; Night Blade reads `SWNB_ATT`; Bone Horn has a working timed effect with
a counter capped by `MBH_TIME`. A player who picks these up gets exactly what
the numbers say; nobody wrote the sentence.

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
other 33 are unreachable** — mission code is unread, and the two most narratively
loaded items in the game (the Ring pieces and the Ring of Concordance) appear in
neither file, which is exactly what you would expect of quest rewards.

## Open threads

- **The Ring Piece → Ring of Concordance step.** Nothing found yet that consumes
  ids 24 and 25 or produces 26. Mission code is the lead, as it is for the eight
  unplaced heroes.
- **Is Mask of the Brave reachable at all?** If no mission places item 1, it is
  dead in the shipped game rather than merely silent. That is a search of the
  mission code, not of these two files.
- **The `+0x04` type field's exact semantics** — the equip checker is unread.
- **The `+0x18` field on ids 2, 8 and 50.** Three items allocate four extra bytes;
  only id 2's initialisation was observed.
- **Slots 5–7 roles** are read from a sample of overriding bodies, not from all
  90. A full pass would firm up the slot table.

## Cross-references

- [heroes.md](heroes.md) — the item factory's other caller, and the same
  unfinished-content pattern in the hero roster.
- [phls-format.md](../reference/phls-format.md) — the `.sdb` locale format used
  throughout here.
- [re-methodology.md](../reference/re-methodology.md) §14 — the vtable-override
  trap this task walked into.
