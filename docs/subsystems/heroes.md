# Heroes

What a hero *is* in Theocracy, what actually differs between the nineteen of
them, and which parts of the roster the shipped game never finished.

**Read off `theocracy.real` 2026-08-08.** Addresses are Ghidra space, game base
`0x08048000`. Every ability below is named by the game's **own config keys**
from `selap.txt`, not by inference from a decompile; where a claim rests on the
in-game description text instead, it says so.

## Two tiers, one byte apart

A hero is not a separate unit — it is a **`cMan` with a hero id**, one byte at
**`+0x27c`**. `cHero` is a real class (RTTI `5cHero`), and the RTTI also carries
`19cMan_Swordsman_Hero`, `18cMan_Spearman_Hero`, `16cMan_Archer_Hero`, plus the
projectiles `10cHeroArrow` and `10cHeroSpear`.

That gives two tiers:

- **Generic heroes** — hero id `0`, but one of three hero *man types*. These are
  ordinary elite units and display as "Swordsman hero" / "Spearman hero" /
  "Archer hero".
- **Named heroes** — hero id `1..19`, each with a name, a description, an icon,
  and its own stat block.

The three man types, read from the `cLocaleEntry` constructors at `0x080b3305`
(not inferred from the order the keys appear in the locale file, which is a
different order):

| man type | key | animations |
|---|---|---|
| `0x21` (33) | `manname_hero_swordsman` | `herosw_*` |
| `0x22` (34) | `manname_hero_spearman` | `herosp_*` |
| `0x23` (35) | `manname_hero_archer` | `heroar_*` |

`cHero::GetName` (`0x080b2b00`) branches on exactly this: hero id `0` falls back
to the man type at `+0xb3` and returns one of three fixed locale entries;
anything else indexes a 19-entry array at `0x085a41e0`, stride `0x18`. The array
ends at `0x085a43a8`, which is precisely where the three generic entries begin —
the bound is read, not assumed. `GetDescription` / `GetIcon` / `GetBigIcon`
follow the same shape, and all four `Fatal` with `"I'm not a hero!"` when the man
is neither.

## `cHero::SetHeroId` — `0x080b23d0`

The whole of a named hero's identity is applied here. It rejects anything
outside `1..19` (`if (0x12 < (byte)(param_2 - 1U))`), refuses to run twice, then
switches on the id.

Every hero gets **four stat modifiers**, loaded from `selap.txt` into a
19 × 16-byte table at `0x084c77a8` and copied into the man at `+0x40..0x4c`:

| offset | key | meaning |
|---|---|---|
| `+0x40` | `HEROn_MOD_HP` | hit points |
| `+0x44` | `HEROn_MOD_ST` | stamina |
| `+0x48` | `HEROn_MOD_ATT` | attack |
| `+0x4c` | `HEROn_MOD_DEF` | defense |

Beyond those four, `SetHeroId` writes only two more kinds of thing: **magic
immunities** and **hero 12's visibility bonus** (`+0x86 += HERO12_VIS_MOD`).
Everything else a hero can do — regeneration, the `*_HIT_PERCENT` bonuses,
forest speed — lives in globals that combat code reads at the point of use, and
is *not* baked into the man here. That distinction matters when reading the
table below: "no immunity" is not the same as "no ability".

## The five magic schools

`SetHeroId` writes `100` into exactly one of five 16-bit slots at `+0x88..0x90`,
and for hero 6 loops a config value into all five. The slots are per-school
magic resistance, and the schools are named by the heroes' own descriptions —
which agree across independent heroes:

| offset | school | confirmed by |
|---|---|---|
| `+0x88` | Sun | Morhamum (18) |
| `+0x8a` | Moon | Toomoo (3) |
| `+0x8c` | Star | Vatlar (13) |
| `+0x8e` | Nature | Kukurbuki (5), Garkuna (10), Pocotli (14) |
| `+0x90` | Soul | Shibiri (1), Akrisi (8) |

`100` means immune. Chimoki (6) is the only hero who gets all five, and gets
them at **`HERO6_MAGICRESISTANCE=90`** rather than 100 — which is exactly what
his description claims: "*partial* immunity to any form of magic". A config
value and a sentence written by different people, agreeing.

This is the one claim in this doc resting partly on description text: the
offset→school mapping is read from code, but *which* school each offset is comes
from the descriptions. Nine heroes and five slots agree with no contradiction,
so it is solid, but the consumer side (the spell code that reads `+0x88..0x90`)
has not been read — see Open threads.

## The nineteen

`type` is from `hero.cfg` where the hero is placed there. `prov / items` is the
province index and the two item slots from `hero.cfg`.

| id | name | type | HP | ST | ATT | DEF | immune | other config keys | prov / items |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Shibiri | spear | -50 | -1500 | 10 | 10 | Soul | `TEAMREG_FRAME`, `TEAMREG_HP` | 34 / 5,0 |
| 2 | Kathapi | spear | 25 | 1500 | 20 | 2 | — | `JARAKHI_HIT_PERCENT` | 22 / 10,0 |
| 3 | Toomoo | — | 25 | 3000 | 20 | 2 | Moon | `DRAGONK_`/`MOONPRIEST_`/`STONEW_HIT_PERCENT` | not placed |
| 4 | Shaloc | spear | 25 | 3000 | -10 | 6 | — | — | 31 / 27,0 |
| 5 | Kukurbuki | — | 0 | 3000 | 10 | 2 | Nature | `REG_FRAME`, `REG_HP` | not placed |
| 6 | Chimoki | spear | 25 | 0 | -10 | 2 | **all @90** | `MAGICRESISTANCE` | 0 / 3,0 |
| 7 | Koloth | sword | 50 | 4000 | 40 | -5 | — | `REG_FRAME`, `REG_HP` | 17 / 36,0 |
| 8 | Akrisi | — | -25 | 1500 | 20 | 2 | Soul | — | not placed |
| 9 | **Umochi** | — | 0 | -2000 | 10 | 6 | — | — | not placed |
| 10 | Garkuna | — | 50 | 2000 | 25 | 0 | Nature | — | not placed |
| 11 | Jarakhi | — | 0 | 3000 | 10 | 6 | — | `KATHAPI_HIT_PERCENT` | not placed |
| 12 | Turmoth | arch | 50 | 2000 | 10 | 5 | — | `VIS_MOD`, `RANGE_MOD` | 6 / 35,0 |
| 13 | Vatlar | sword | 50 | -4000 | 25 | 12 | Star | — | 36 / 39,22 |
| 14 | Pocotli | sword | -25 | 4000 | 25 | 8 | Nature | `JAGUAR_`/`NATUREPRIEST_HIT_PERCENT` | 16 / 0,0 |
| 15 | Fakhuma | sword | 50 | 2000 | 25 | 0 | — | `REG_FRAME`, `REG_ST`, `SWORDSMAN_ATT_PERCENT` | 7 / 42,0 |
| 16 | HuorMuah | sword | 50 | -4000 | 40 | 0 | — | — | 35 / 16,4 |
| 17 | Skalaki | — | 50 | 1000 | 30 | 5 | — | `SPEEDPERCENT_IN_FOREST` | not placed |
| 18 | Morhamum | arch | 50 | 0 | 30 | -2 | Sun | `PRIEST_HIT_PERCENT` | 8 / 33,0 |
| 19 | Tlechlal | — | 25 | 0 | 40 | 8 | — | — | not placed |

Four heroes — **Shaloc (4), Umochi (9), HuorMuah (16), Tlechlal (19)** — have no
ability of any kind beyond the four numbers. For three of them that is clearly
deliberate: their descriptions promise nothing but statistics, and the numbers
deliver them. HuorMuah's "one blow of his sword will kill anyone" is `ATT=40`,
the joint highest in the game, against `DEF=0` and `ST=-4000` — "he has never
learned to take blows and becomes tired very quickly". The lore is the stat
block, written out in prose.

Umochi is the exception, and is covered under Unfinished content below.

### The rivalry is in the config file

`HERO2_JARAKHI_HIT_PERCENT=115` and `HERO11_KATHAPI_HIT_PERCENT=115`. Kathapi is
hero 2; Jarakhi is hero 11. Each fights 15% better against the other, and
Kathapi's description names it: "the commander of the Axocopan army and the
mortal enemy of your foster brother, Jarakhi". The other `*_HIT_PERCENT` keys
target unit *kinds* rather than individuals — `DRAGONK` (dragonkiller),
`MOONPRIEST`, `STONEW`, `NATUREPRIEST`, `PRIEST`, `JAGUAR`, `SWORDSMAN` — with
Pocotli's anti-Nature pair at 150 and Morhamum's anti-priest at 250, the
steepest in the file.

## `hero.cfg` — placement, not roster

Loaded once at world setup by the function at `0x081fb5b0`, immediately after
`data/mitem.cfg`, and only when `*(int *)(g_GameSession + 0x4c) == 0`. Parsed by
`0x08215300`, consumed by `0x08215200`.

The parser reads **eight whitespace-separated columns into a packed 17-byte
record** — and the column order is not the field order, which is why the file
looks strange:

| column | record offset | meaning |
|---|---|---|
| 0 | `+0x08` u8 | man type (33/34/35) |
| 1 | `+0x09` u8 | hero id → `SetHeroId` |
| 2 | `+0x0e` u8 | nation / tribe |
| 3 | `+0x00` i32 | always `0` in the shipped file |
| 4 | `+0x04` i32 | always `0` in the shipped file |
| 5 | `+0x0a` i32 | province index |
| 6 | `+0x0f` u8 | magic item slot 1 |
| 7 | `+0x10` u8 | magic item slot 2 |

The consumer looks the province up in the list at `g_World+0x1468` (count at
`+0x1470`), calls virtual `+0xe4` on it to create the man, calls `SetHeroId`,
then hands the man up to **two magic items** via the factory at `0x0820d1f0` —
item id `0` yields NULL and is skipped, which is how a hero carries one item or
none.

Column 2 is the nation, established by five independent pairs whose descriptions
agree: `1` = Axocopan (Kathapi, Shaloc), `2` = Yaxuna (HuorMuah, Vatlar), `3` =
Huatepec (Turmoth, Fakhuma), `4` = Iztahuacan (Koloth, Shibiri), `5` = Teotitlan
(Chimoki, Morhamum), `7` = Pocotli alone.

**`hero.cfg` places only 11 of the 19 heroes.** It is a starting-placement table,
not the roster — the roster is the `1..19` range hard-coded in `SetHeroId`. The
other eight must be introduced elsewhere; the mission code is the obvious
suspect, since `cMission_S4_0::Start` carries the assert `"Hero not found."` (in
Hungarian, and rude about a colleague). Unread — see Open threads.

## Unfinished content

Three findings, all of the shape this codebase keeps producing — content that
exists in a data file and is never reached by code.

1. **Umochi (hero 9) is a placeholder.** His description is the six-character
   string `Umochi`; every other hero runs 146–370 characters. He is also the only
   hero with neither an immunity nor a single `HERO9_*` key beyond the four stat
   modifiers, and he is not placed in `hero.cfg`. He has a name, an icon, a stat
   block and nothing else.
2. **Six dead config keys.** `HERO2_TEAMREG_FRAME`, `HERO2_TEAMREG_MO`,
   `HERO8_TEAMREG_FRAME`, `HERO8_TEAMREG_MO`, `HERO11_TEAMREG_FRAME`,
   `HERO11_TEAMREG_MO` are all present in `selap.txt`. **None of those six
   strings exists anywhere in `theocracy.real`**, so nothing ever registers or
   reads them. Only `HERO1_TEAMREG_FRAME` / `HERO1_TEAMREG_HP` are real. Note the
   suffix differs too — the live pair ends `_HP`, the dead ones `_MO` — so this
   looks like an ability that was redesigned and left behind in the data.
3. **Eight heroes are fully specified but never placed.** Not a defect on its own
   — see the mission-code thread — but worth stating alongside the above.

## How this was found

Strings first, binary last, the same chain as
[population-and-births.md](population-and-births.md):

1. `strings theocracy.real | grep -i hero` — which immediately settled the
   structural question (a `cHero` class *and* `cMan::SetHeroId` both exist).
2. `data/hero.cfg` and the assert strings gave the two entry points by xref.
3. The four stat globals turned out to be **zero in the file** — `.data`, filled
   at runtime — so the values came from `LoadConfigVar` call sites, disassembled
   and parsed pairwise to map every global to its key name.
4. The descriptions confirmed the parts the code could only hint at.

One methodology lesson came out of it and is recorded in
[re-methodology.md](../reference/re-methodology.md): grepping a shipped data file
for a config key gives a **false negative**, because the file is encrypted. This
cost a wrong intermediate conclusion ("`selap.txt` has no hero keys" — it has
103).

## Open threads

- **Who reads `+0x88..0x90`.** The offset→school mapping rests on description
  text. Reading the spell-application path would confirm it from the consumer
  side and pin the school enum order.
- **Where the other eight heroes enter.** `cMission_S4_0::Start` and its
  `"Hero not found."` assert are the lead.
- **Province virtual `+0xe4`.** The man-spawning call in `hero.cfg`'s consumer;
  unread, and the owner of the two always-zero i32 columns, which are plausibly
  map coordinates but have never been observed non-zero.
- **`HERO12_RANGE_MOD` and the regen keys** are registered and therefore live,
  but their consumers have not been read — only `VIS_MOD` was traced into
  `SetHeroId`.

## Cross-references

- [mana-and-sacrifice.md](mana-and-sacrifice.md) — the per-man-type value and
  experience model that heroes sit on top of.
- [phls-format.md](../reference/phls-format.md) — the `.sdb` locale database
  format, cracked in the course of this work.
- [simulation-step.md](simulation-step.md) — the units manager, still the
  unmapped mass around all of this.
