# Heroes

What a hero *is* in Theocracy, what actually differs between the nineteen of
them, and which parts of the roster the shipped game never finished.

**Read off `theocracy.real` 2026-08-08.** Addresses are Ghidra space, game base
`0x08048000`. Every ability below is named by the game's **own config keys**
from `selap.txt`, not by inference from a decompile; where a claim rests on the
in-game description text instead, it says so.

## Two tiers, one byte apart

A hero is not a separate unit — it is a **`cMan` with a hero id**, one byte at
**`+0x27c`** — which, as of 2026-08-10, is known to be the byte `cHero` *adds*
rather than a `cMan` field it borrows: `sizeof(cMan)` is exactly `0x27c`. See
"What `+0x27c` actually is" below. `cHero` is a real class (RTTI `5cHero`), and
the RTTI also carries
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
and for hero 6 loops a config value into all five.

**Read from the consumer side 2026-08-10**, which was this doc's last open
thread. It is not five fields but **one five-element `u16` array**, and the
accessor says so in one line — `cMan_GetMagicResistance` (`0x080aded0`):

```c
short v = *(short *)(man + school*2 + 0x88);
return v < 0 ? 0 : v < 101 ? v : 100;      // clamped to 0..100
```

And the consumer is `cMan_ApplyMagicDamage` (`0x08098180`):

```c
if (spell->school != 5)                                  // 5 = no school
    damage = damage * (100 - resist[spell->school]) / 100;
```

So the slot is a **percentage damage reduction**, and `100` is immunity as an
*endpoint* rather than as a flag — a hero at 90 takes a tenth of the damage. The
spell carries its own school at `spell+0x350`.

The school enum is pinned by four spell classes that inline their own slot
instead of calling the accessor, each naming itself in RTTI:

| index | offset | school | read directly by |
|---|---|---|---|
| 0 | `+0x88` | Sun | `cSpell_Sun5` (`0x08260ae0`) |
| 1 | `+0x8a` | Moon | — by elimination |
| 2 | `+0x8c` | **Stars** | `cSpell_Stars5` (`0x0826b250`) |
| 3 | `+0x8e` | Nature | `cSpell_Nature4` (`0x0826f170`) |
| 4 | `+0x90` | Soul | `cSpell_Soul6` (`0x08279340`) |

Four are read off code. Moon is elimination over a **closed** set — the array is
exactly five wide (the Chimoki loop runs `while (i < 5)`), four indices are named,
so the fifth is determined — and it independently matches both the description
evidence below and the RTTI ordering of the spell roster, which is
`cSpell_Sun/Moon/Stars/Nature/Soul`, six spells each, plus `cSpell_ChPriest` and
`cSpell_Vampire1/2`.

**One name changes**: the third school is `Stars`, not `Star`, per its own RTTI.

The descriptions, which were the previous basis and are now corroboration:

| offset | school | heroes whose description says so |
|---|---|---|
| `+0x88` | Sun | Morhamum (18) |
| `+0x8a` | Moon | Toomoo (3) |
| `+0x8c` | Stars | Vatlar (13) |
| `+0x8e` | Nature | Kukurbuki (5), Garkuna (10), Pocotli (14) |
| `+0x90` | Soul | Shibiri (1), Akrisi (8) |

Chimoki (6) is the only hero who gets all five, at **`HERO6_MAGICRESISTANCE=90`**
rather than 100 — exactly what his description claims, "*partial* immunity to any
form of magic". Read as a percentage that is now literally true: he takes 10% of
all magic damage. A config value, a sentence and a formula written by different
people, agreeing.

### It is not a hero field — every man has one

**Added 2026-08-10.** The array is part of the base `cMan`, and heroes only
*overwrite* entries in it. Two more paths write it, and both walk it as an array
of five, which is now the third and fourth independent confirmation of the shape:

- **The plain `cMan` constructor** (`0x08093df0`) fills all five from the man's
  **caste properties** — bytes `+0x35..+0x39` of the caste struct, which
  `cMan::cCasteProperties`'s own constructor (`0x080ae470`, named by its
  `Fatal("cMan::cCasteProperties : man speed shouldn't be greater than 250")`)
  sets to **one value repeated across all five schools**. So a man type has a
  flat magic resistance, and per-*school* variation is a hero-only thing.
- **The save stream**: `cMan_CombatAttribs_Load` (`0x080b7fc0`) restores the
  sub-object at `man+0x80` and reads the five slots back in a loop of five. They
  survive save/load — which is worth stating because the stream constructor
  zeroes them first, and a reader who stopped there would conclude that a loaded
  hero silently loses his immunity.

## The nineteen

`type` is from `hero.cfg` where the hero is placed there. `prov / items` is the
province index and the two item slots from `hero.cfg`. **"no code path" is not
"absent"** — the shipped world state is a savegame and can carry a hero
directly; see the correction below.

| id | name | type | HP | ST | ATT | DEF | immune | other config keys | prov / items |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Shibiri | spear | -50 | -1500 | 10 | 10 | Soul | `TEAMREG_FRAME`, `TEAMREG_HP` | 34 / 5,0 |
| 2 | Kathapi | spear | 25 | 1500 | 20 | 2 | — | `JARAKHI_HIT_PERCENT` | 22 / 10,0 |
| 3 | Toomoo | — | 25 | 3000 | 20 | 2 | Moon | `DRAGONK_`/`MOONPRIEST_`/`STONEW_HIT_PERCENT` | — / mission Scroll |
| 4 | Shaloc | spear | 25 | 3000 | -10 | 6 | — | — | 31 / 27,0 |
| 5 | Kukurbuki | — | 0 | 3000 | 10 | 2 | Nature | `REG_FRAME`, `REG_HP` | — / mission TwoRings |
| 6 | Chimoki | spear | 25 | 0 | -10 | 2 | **all @90** | `MAGICRESISTANCE` | 0 / 3,0 |
| 7 | Koloth | sword | 50 | 4000 | 40 | -5 | — | `REG_FRAME`, `REG_HP` | 17 / 36,0 |
| 8 | Akrisi | — | -25 | 1500 | 20 | 2 | Soul | — | — / mission Josda |
| 9 | **Umochi** | — | 0 | -2000 | 10 | 6 | — | — | **nothing, anywhere** |
| 10 | Garkuna | — | 50 | 2000 | 25 | 0 | Nature | — | — / mission VillageOfJaguar |
| 11 | Jarakhi | — | 0 | 3000 | 10 | 6 | — | `KATHAPI_HIT_PERCENT` | **player character — ships in `campaign/init.dat`** |
| 12 | Turmoth | arch | 50 | 2000 | 10 | 5 | — | `VIS_MOD`, `RANGE_MOD` | 6 / 35,0 |
| 13 | Vatlar | sword | 50 | -4000 | 25 | 12 | Stars | — | 36 / 39,22 |
| 14 | Pocotli | sword | -25 | 4000 | 25 | 8 | Nature | `JAGUAR_`/`NATUREPRIEST_HIT_PERCENT` | 16 / 0,0 |
| 15 | Fakhuma | sword | 50 | 2000 | 25 | 0 | — | `REG_FRAME`, `REG_ST`, `SWORDSMAN_ATT_PERCENT` | 7 / 42,0 |
| 16 | HuorMuah | sword | 50 | -4000 | 40 | 0 | — | — | 35 / 16,4 |
| 17 | Skalaki | — | 50 | 1000 | 30 | 5 | — | `SPEEDPERCENT_IN_FOREST` | — / mission TheWall |
| 18 | Morhamum | arch | 50 | 0 | 30 | -2 | Sun | `PRIEST_HIT_PERCENT` | 8 / 33,0 |
| 19 | Tlechlal | — | 25 | 0 | 40 | 8 | — | — | **ships in `scn6/init.dat`** |

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
| 3 | `+0x00` i32 | map x — the first half of the position handed to province virtual `+0xe4`; always `0` in the shipped file |
| 4 | `+0x04` i32 | map y — likewise |
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
3. **Umochi (9) is placed by nothing at all** — no code path, and no world file
   either, which was checked across all nine on 2026-08-09
   ([starting-world.md](starting-world.md)). He is the only hero of whom that is
   true. This started as eight unplaced heroes; mission code accounts for five,
   **Jarakhi (11) ships in `data/campaign/init.dat`** as the player character,
   and **Tlechlal (19) ships in `scn6/init.dat`**. See the correction below.

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

## Where the other eight enter — answered 2026-08-09

**Five of the eight are mission rewards; three are placed by nothing.** Full
write-up in [missions.md](missions.md); the short form:

`cHero_SetHeroId` is a virtual at hero-vtable slot `+0x58`, and it is the only
writer of the id — established by scanning every write to `+0x27c`, not by
trusting the name. Across the whole binary **five call sites pass a constant
id**, plus `hero.cfg` and the developer console:

| mission | hook | hero | items given |
|---|---|---|---|
| `cMission_VillageOfJaguar` | `Finish` | **10 Garkuna** | 15 |
| `cMission_Scroll` | `Finish` | **3 Toomoo** | 21, 11 |
| `cMission_TwoRings` | `Finish` | **5 Kukurbuki** | 45, 28 |
| `cMission_Josda` | `Finish` | **8 Akrisi** | 31, 13 |
| `cMission_TheWall` | **`Start`** | **17 Skalaki** | 34 |

So sixteen of the nineteen are placed by code. **Umochi (9), Jarakhi (11) and
Tlechlal (19) are assigned by no code path** — but see the correction directly
below before reading anything into that.

### Correction — a hero can ship in the world state

**2026-08-09, same day.** This section first said those three "are set by
nothing" and moved them into the unfinished-content list. That was wrong, and
`cHero_SetHeroId` is **not** the only writer of the id.

`cHero` has a **second, stream constructor** (`0x080b22c0`) which runs the base
`cMan` stream ctor, installs the hero vtable, and reads **one byte straight into
`+0x27c`** from the file. It is reached through the per-man-type caste-properties
table rather than by any direct call, so it appears in no xref sweep — and the
original scan for writes to `+0x27c` missed it because the constructor takes the
field's *address* (`leal 0x27c(%ebx), %edx`) instead of storing to it.

And there is data for it to read: **every `init.dat` in the tree — the campaign's
and all eight scenarios' — is a `theosg42` savegame**
([save-format.md](save-format.md)). The starting world is loaded, not placed.

**Jarakhi (11) is the campaign's player character**, which is why nothing spawns
or rewards him: he ships in the starting world state. That also *restores* the
rivalry below rather than killing it — both Kathapi and Jarakhi are present, and
the two symmetric `*_HIT_PERCENT` keys do what they say. His portrait being
preloaded by a caste registration (`0x08254def`, grouped with heroes 1–8) is
consistent with this.

Umochi (9) is the one of the three that a stream-loaded hero would not rescue:
his description is the string `Umochi`, and no map data changes that.

### Confirmed against all nine world files — 2026-08-09

The correction above was an inference from a constructor that *could* place a
hero. All nine `init.dat` files have since been read by the game's own loader
([starting-world.md](starting-world.md)), and it holds:

- **Jarakhi (11) is in `data/campaign/init.dat`** and in nothing else. Confirmed,
  not inferred.
- **Tlechlal (19) is in `scn6/init.dat`** — the one genuinely unexplained hero,
  and the answer is that he is scenario content. Every campaign-shaped search
  was looking in the wrong file.
- **Umochi (9) is in none of the nine.** He was the one a world file could not
  rescue and it does not: code, `hero.cfg`, mission rewards and serialised state
  have all now been checked. The placeholder reading is as firm as it gets.

The campaign's twelve heroes are **exactly `hero.cfg`'s eleven plus Jarakhi**,
which is what a world file generated by starting a new game and then editing one
character in would look like. So the `prov / items` column above describes how
the shipped world was *authored*; whether those placers also run at play time is
an open thread in [starting-world.md](starting-world.md).

A sixth site places a hero-type man and never assigns an id
(`cMission_Scroll_lost_Finish`), leaving the `0` the constructor wrote.

## What `+0x27c` actually is

**Settled 2026-08-10**, and the question as this doc posed it — *the* hero id, or
a general subtype byte? — turns out to be a false choice. It is neither: it is
**the first byte past the end of `cMan`**, so every subclass that adds one byte
gets its own field at that offset.

`sizeof(cMan) == 0x27c`, read off the allocators rather than inferred from a
struct guess. Six caste creators allocate a plain man with `new(0x27c)`
(`0x08246280`, `0x08249530`, `0x0824a740`, …); every `cHero` subclass creator and
`cMan_Comm1`'s allocate **`new(0x280)`** — `0x27c` plus one byte, padded to four.

So `cHero` and `cMan_Comm1` are not sharing a field, they are two classes each
declaring a first member. `cMan_Comm1`'s is a different thing entirely: its
constructor seeds it with the constant `26`, and it is read and written through a
getter/setter pair of vtable slots — getter `+0xe0` (`cMan_Comm1_GetSubtype`,
`0x082461b0`), setter `+0xe8` (`cMan_Comm1_SetSubtypeFromMan`, `0x08246150`,
which copies *another* man's man type from `+0xb3`). In the base `cMan` vtable
those same two slots are a flat `return 25` and an empty body, so nothing
inherited ever touches the byte.

### The scan that settles it

Done over the **instruction stream**, not over decompiles, because the earlier
"sole writer" claim was wrong exactly where a decompile sweep is weak (the stream
constructor takes the field's *address*, so there is no store to find). Scanning
`.text` for the four bytes of the displacement `0x27c` gives 43 occurrences; 10
are `jcc`/`call` rel32 and `push 0x27c` immediates, and classifying the remaining
33 by the opcode in front of them gives:

| where | accesses |
|---|---|
| the `cHero` translation unit, `0x080b22a2`–`0x080b2de2` | 2 writes, 2 address-takes, 21 reads |
| the `cMan_Comm1` translation unit, `0x08245944`–`0x082461b8` | 2 writes, 2 address-takes, 1 read |
| `cMan_Archer_Hero_GetRange` (`0x08255900`) | 1 read |
| two unrelated classes | 2 **32-bit** writes — a different field at the same offset in a non-`cMan` object |

**Nothing generic reads or writes it.** Every read in the `cHero` block is a
per-hero ability hook of the form "if my id is *N*, apply `HEROn_…`" — e.g.
`FUN_080b2730` gives hero 13 (Vatlar) a resource payout.

And the developers name the field themselves twice over. Base `cMan` vtable slot
`+0x58` is a one-line `Fatal("cMan::SetHeroId : I'm not a hero")` — `cHero`
overrides that slot with `cHero_SetHeroId`. And the one reader outside `cHero`'s
own translation unit is fed by a config key that spells out the id it is testing.

So this doc's headline claim stands as written, and the class-scoping caveat can
be dropped — with the sharper statement that it was never a `cMan` byte to scope.

### `HERO12_RANGE_MOD`, and where hero abilities really live

That last reader closes the doc's other open thread. `cMan_Archer_Hero`
(RTTI-confirmed, vtable `0x083f7640`) overrides vtable slot `+0x38` — which
returns `0` in the base `cMan` — with:

```c
char range = RANGE_BASE;
if (this->heroId == 12) range = RANGE_BASE + HERO12_RANGE_MOD;
return range;
```

Hero 12 is **Turmoth, the archer**, whose two listed modifiers are `VIS_MOD` and
`RANGE_MOD`. So hero abilities live in **two** places, not one:

- **Baked into the object** by `cHero_SetHeroId` — the four stat modifiers, the
  magic-school immunities, and `HERO12_VIS_MOD`, which is added to `+0x86` there.
- **Applied live in a per-class getter** — `HERO12_RANGE_MOD`, which is not
  stored anywhere and is recomputed on every call, and only by the archer class.

The split matters for anyone chasing a hero ability: `SetHeroId` is not the whole
story, and a key absent from it is not therefore dead.

## Open threads

- ~~**Who reads `+0x88..0x90`.**~~ **Closed 2026-08-10** — `cMan_GetMagicResistance`
  and `cMan_ApplyMagicDamage`, above. The mapping is now read from the consuming
  code for four of five schools and determined by elimination for the fifth, and
  the slots turn out to be a percentage reduction rather than a flag.
- **The regen keys** (`HEROn_REG_FRAME`/`REG_HP`/`REG_ST`) and the various
  `*_HIT_PERCENT` keys have registered consumers somewhere in the ~21 per-hero
  hooks in the `cHero` translation unit, now enumerated by address above but not
  read one by one.
- **What `cMan_Comm1`'s subtype byte is for.** Read and written through vtable
  `+0xe0`/`+0xe8`, seeded to `26`, set from another man's man type — but the
  setter is only ever reached by virtual dispatch, so its caller was not chased.
- **Where a spell's school (`spell+0x350`) is set.** Only its *use* is read, in
  `cMan_ApplyMagicDamage`. It is not written by any `mov [reg+0x350], imm32` in
  the image, so it comes from a register — probably the spell base constructor.
  Chasing it would turn the **Moon** slot from elimination into a direct reading,
  which is the one link in the school table above that is inferred rather than
  read.

## Cross-references

- [mana-and-sacrifice.md](mana-and-sacrifice.md) — the per-man-type value and
  experience model that heroes sit on top of.
- [phls-format.md](../reference/phls-format.md) — the `.sdb` locale database
  format, cracked in the course of this work.
- [simulation-step.md](simulation-step.md) — the unit AI/movement core, still the
  unmapped mass around all of this. (Its "units manager at `g_World+0x1f394`" was
  withdrawn 2026-08-10 — that address is the mission handler; the units
  *container* is `+0x1f398`.)
