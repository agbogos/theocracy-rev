# Mana, pyramids and sacrifice

How a pyramid makes mana by standing there, what happens to the people you feed
into one, and why the mana indicator shoots up and then crawls — which turns out
to be a property of the *gauge*, not of the simulation.

**Read off `theocracy.real` 2026-08-08.** Addresses are Ghidra space, game base
`0x08048000`. The rounding behaviour below is confirmed against the instruction
stream, because the decompiler is actively misleading about it (see "A note on
`ROUND`").

## Where mana lives

The five mana spheres are **`cTribe::resource[5]`** at `cTribe+0x34` — the array
[../structs/cTribe.md](../structs/cTribe.md) describes as "five per-tribe
resource/mana pools". That doc's `g_TribeResourceInit` (`0x84c85f1`) and
`g_TribeResourceMax` (`0x84c85f5`) are **`START_MANA` (0) and `MAX_MANA`
(70000)**, which is why they are scalars rather than per-resource tables: every
sphere shares one floor and one ceiling. The sphere order is the one the console
`mannaking` command names — sun, moon, stars, nature, soul
([dev-console.md](dev-console.md)).

`cTribe_AddResource` (`0x0815a100`) is the only way in, and it is a plain add
with a clamp at `MAX_MANA`. **There are no diminishing returns on stored mana.**

## The class

**`cPyramidBuilding`** — the RTTI name at `0x083857eb`, in the same vtable region
as the two vtables its constructor installs (`0x08385200`, then `0x08384c60`).
Ten concrete subclasses, `cBld_Pyramid{A,B,C,D,E}{1,2}` — five spheres × small
and big — which is why each pyramid constant below has exactly five read sites.

| Address | Name | Does |
|---|---|---|
| `0x081b9ab0` | `cPyramidBuilding_ctor` | zeroes the queues, sets the payout period |
| `0x081b9ba0` | `cPyramidBuilding_Tick` | passive mana **and** sacrifice payout |
| `0x081b9e40` | `cPyramidBuilding_DoSacrifice` | the sacrifice command |
| `0x081bc780` | `cPyramidBuilding_ResetSacrificeTimer` | one line; 11 call sites |
| `0x0815a0a0` | `Mana_GaugeFraction` | the UI curve — **not** production |

Names other than `cPyramidBuilding` are ours, written back into the Ghidra DB.

### Layout

| Offset | Type | Meaning |
|---|---|---|
| `+0x90` / `+0x94` | `u32` / `ptr` | contained men — count and array |
| `+0x230` | `bool` | a sacrifice queue is active |
| `+0x231` | `i32 [42]` | queued sacrifice **count**, indexed by man type (`man+0xb3`) |
| `+0x2d9` | `i32 [42]` | **average** mana value per man of that type |
| `+0x381` | `i32` | payout accumulator |
| `+0x385` | `u32` | queue cursor; `42` = drained |
| `+0x389` | `i32` | passive-mana accumulator |
| `+0x38d` | `i32` | ticks per payout = `SACRIFICE_MINUTES × g_TicksPerMinute` |

Two virtuals matter: **`vt+0xcc`** returns the pyramid's sphere index, and
**`vt+0x114`** returns the ticks per one unit of passive mana. The latter is a
one-line override per subclass —

```c
int cBld_PyramidA2::ManaPeriod() { return BIGPYRAMID_DAYS_TO_ONE_MANA * g_TicksPerDay; }
```

— which is the whole reason `BIGPYRAMID_DAYS_TO_ONE_MANA` has five readers and
`SMALLPYRAMID_DAYS_TO_ONE_MANA` has five more.

## Passive mana

First half of `cPyramidBuilding_Tick(this, elapsed)`:

```c
this[+0x389] += elapsed;
period = vt[0x114](this);                 // days-to-one-mana * g_TicksPerDay
mana    = this[+0x389] / period;
this[+0x389] %= period;                   // remainder carried, nothing is lost
if (mana) cTribe_AddResource(tribe, mana, vt[0xcc](this));
```

With the shipped balance that is **1 mana/day for a big pyramid and 1 mana every
2 days for a small one**, into that pyramid's sphere. The modulo means a pyramid
loses nothing to partial days, however coarsely the tick is driven.

## Sacrifice

`cPyramidBuilding_DoSacrifice(this)` empties the pyramid in one call. It is
built around the two 42-entry arrays, which store a **running average** rather
than a total, so the function un-averages before adding and re-averages after:

```c
for (t = 0; t < 42; t++) value[t] *= count[t];          // back to totals

while ((victim = next man in the pyramid) != NULL) {    // children (type 0x19) skipped
    t = victim[+0xb3];
    count[t] += 1;
    value[t] += typeBase + trunc(victim[+0x80] * typeFactor / 1000);
    FUN_08098980(victim);                               // destroyed, here and now
}

for (t = 0; t < 42; t++) if (count[t]) value[t] /= count[t];   // back to averages
this[+0x385] = first t with count[t] != 0;              // cursor
this[+0x230] = (this[+0x385] < 42);
```

`typeBase` and `typeFactor` are `+0x28` and `+0x2c` on the man's **type
descriptor** (reached through `man→vt[0x20]`); `victim+0x80` is a per-instance
value that behaves like experience. So a man is worth a flat per-type amount plus
a share of what he has earned, and **a veteran is worth more than a slave**.

The divisor is `0x084c8639` (= 1000). It is *not* a registered config var — nine
read sites, no `LoadConfigVar` binding — so unlike everything else here it
cannot be retuned from `selap.txt`.

**No mana is granted at this point.** The men die immediately; the pyramid keeps
a bill.

### `typeBase` does not come from `selap.txt` — and 16 constants are dead

`SUNPRIEST_VALUE` and friends look exactly like the source for `typeBase`, and
they are not. Settled 2026-08-08.

The function this project has been calling `RegisterConfigVar` keeps **no
registry**, so it is renamed **`LoadConfigVar`** (`0x080b3de0`) as of this work.
It looks the name up in the parsed environment and does one `strtol` into the
destination int:

```c
var = FindVariable__9cEnvClassPCc(g_Env, name);
if (var) *dest = strtol(GetValue__7cEnvVari(var, -1));
```

So a config global that nothing reads is simply dead. The `.data` region holds
two parallel, adjacent, packed arrays covering the same unit roster:

| Array | Range | Read? |
|---|---|---|
| `*_PRICE` — 17 entries | `0x084c8e71`–`0x084c8ebd` | **yes**, one site each |
| `*_VALUE` — 16 entries | `0x084c8ec1`–`0x084c8efd` | **no — not one, anywhere** |

The `_PRICE` set feeds `FUN_082b5790`, the **training-menu builder**: each price
becomes field `+0x10` of a `0x40`-byte recruitment entry, next to a type id, an
icon index and a localised name, with the priest entries gated behind
`param_1+0x471`. The `_VALUE` set — `SWORDMASTER`, `SPEARMASTER`, `BOWMASTER`,
`DRAGONKILLER`, the five priests, `MASTEROFJAGUAR`, `JAGUAR`, `CAPTAIN`,
`SWORDSMAN`, `SPEARMAN`, `BOWMAN`, `SCOUT` — is parsed out of `selap.txt` into
globals and then never touched. Each of the 16 addresses occurs **exactly once
in the entire file**: its own registration `push`.

Checked by scanning `.text` for *any* 4-byte operand landing anywhere in
`0x084c8e70..0x084c8f10`, rather than for the 16 addresses individually — an
array base at a neighbouring global would otherwise have been missed. All 68 hits
are accounted for: the registration pushes, the 17 `_PRICE` reads, and a handful
of variables past the end of the block.

So editing `SUNPRIEST_VALUE=100` in `selap.txt` changes nothing, and `typeBase`
at type-descriptor `+0x28` is loaded from somewhere else — the unit type table,
not the balance file.

The general lesson is worth more than the instance: in this binary a config key
existing, and even being parsed, is **not** evidence that the game reads it.
`LoadConfigVar` writes to a plain int and walks away, so "is this balance number
live?" is answerable in one `elfq xref-global` and should be asked before any
finding rests on a `selap.txt` value.

## Payout

Second half of the same tick:

```c
if (this[+0x230]) {
    this[+0x381] += elapsed;
    while (this[+0x381] > this[+0x38d]) {
        this[+0x381] -= this[+0x38d];
        cTribe_AddResource(tribe, value[cursor], vt[0xcc](this));
        if (--count[cursor] == 0) advance cursor to the next non-empty type;
        if (cursor == 42) break;
    }
    this[+0x230] = (cursor < 42);
}
```

`SACRIFICE_MINUTES` is **480** — 8 in-game hours, exactly one third of a day
under the 86,400k-per-day scale in [calendar.md](calendar.md). So the queue pays
out **three sacrifices per in-game day at a constant rate**, one man each, in
ascending *man-type* order, until it drains.

It is a `while`, not an `if`, so fast-forwarding on the realm view settles
everything the queue owes rather than dropping it. Two consequences worth
knowing: a large sacrifice is a long slow annuity rather than a lump sum, and
because the cursor walks type indices in order, a mixed batch pays out grouped by
type rather than in the order the men were fed in.

## The taper is the gauge

`Mana_GaugeFraction(tribe, sphere, n)`:

```c
return trunc( (1 - G/(G + m)) * n );        // == trunc(n*m / (G + m))
```

where `m` is the tribe's current mana in that sphere and `G` is `MANA_GRADIENT`
(1000). Its only callers are inside a blitter — `cMemBlock`,
`Use__13cSystemMemory`, sprite banks at `DAT_08647e40 + sphere*0x5c` — computing

```c
frame = 10 - trunc(11*m / (1000 + m));      // an 11-frame indicator
```

so this is **display, not simulation**. The curve:

| Mana | Gauge |
|---|---|
| 100 | 1/11 |
| 500 | 3/11 |
| 1,000 | 5/11 |
| 5,000 | 9/11 |
| **≥ 10,000** | **full** |

The stored value keeps climbing linearly to `MAX_MANA` = 70,000, but the
indicator saturates at 10,000 — a seventh of the cap. A constant payout rate
therefore *looks* like a burst that tapers off, and a player watching the
indicator sees a curve the simulation does not have.

## A note on `ROUND`

Both the gauge and the per-man sacrifice value are computed in x87 long double
and converted to integer. Ghidra prints the conversion as `ROUND(...)`, which
reads as round-to-nearest. **It is truncation.** g++ 2.x implements a C cast to
integer by switching the rounding mode first:

```asm
d9 7d fc     fnstcw  [ebp-4]
8b 5d fc     mov     ebx,[ebp-4]
b7 0c        mov     bh,0xc          ; RC = 11 = round toward zero
89 5d f4     mov     [ebp-0xc],ebx
d9 6d f4     fldcw   [ebp-0xc]
df 7d f4     fistp   qword [ebp-0xc]
d9 6d fc     fldcw   [ebp-4]         ; restore
```

This is not pedantry. On the gauge, `11*m/(1000+m)` approaches 11 but never
reaches it, so **truncation keeps the frame index in `0..10`**. Round-to-nearest
would push it to 11 at `m ≥ 21000`, making `frame = 10 - 11 = -1` and indexing a
sprite descriptor 16 bytes *before* the array — a plausible-looking
out-of-bounds bug that was written up here and then deleted, because the
instruction stream says it does not exist. Exactly the case
[../reference/re-methodology.md](../reference/re-methodology.md) §5 is about:
read the disassembly for anything load-bearing.

## The constants

| Key | Global | Shipped | Built-in default |
|---|---|---|---|
| `BIGPYRAMID_DAYS_TO_ONE_MANA` | `0x084c8500` | 1 | 2 |
| `SMALLPYRAMID_DAYS_TO_ONE_MANA` | `0x084c84fc` | 2 | 5 |
| `SACRIFICE_MINUTES` | `0x084c839c` | 480 | 120 |
| `MANA_GRADIENT` | `0x084c85f9` | 1000 | 1000 |
| `MAX_MANA` | `0x084c85f5` | 70000 | 70000 |
| `START_MANA` | `0x084c85f1` | 0 | 0 |
| `ONE_JEWEL_EQUALS_HOW_MANY_MANAS` | `0x084c8f01` | 3 | 10 |
| *(XP divisor, unregistered)* | `0x084c8639` | — | 1000 |

Method for resolving any of these: see
[population-and-births.md](population-and-births.md), "How this was found".

## Open threads

- ~~**Both mana paths credit the *local* player's tribe**~~ — **closed
  2026-08-08, from play rather than from the binary.** Both paths write to
  `g_GameSession+0x2d`, which looked like it might mis-credit AI-owned pyramids.
  It does not matter, because **the game does not simulate the AI's economy at
  all**: inspected in edit mode, enemy provinces have no proper slave setup and
  support far more units than the player's economy could sustain, so AI forces
  are provisioned rather than produced. This code only ever runs for the player,
  and the local-tribe write is correct by construction rather than by accident.
  A native rewrite should preserve that assumption knowingly — it is not a bug to
  "fix". (`docs/` has no page on the AI's economy; if one is ever written this
  observation belongs there.)
- ~~**`SUNPRIEST_VALUE` and its four siblings**~~ — **closed 2026-08-08: dead
  data.** See "`typeBase` does not come from `selap.txt`" above. The whole
  16-entry `*_VALUE` array is loaded and never read; `LoadConfigVar` keeps no
  registry, so there is no by-name path either.
- **`man+0x80`** — read as experience from how it is used, not confirmed. It is
  the same struct the births work left three unnamed enums in.
- **The second `MANA_GRADIENT` reader** at `0x082b8e31` is unexamined; if it is
  not also UI, the "display only" claim above needs narrowing.
- **`FUN_081b9420`** — the limit that decides how many children the sacrifice
  loop will skip before taking one anyway.
- **`ONE_JEWEL_EQUALS_HOW_MANY_MANAS`** — read at `0x082b1153`/`0x082b117e`, the
  jewel↔mana exchange, not looked at.

## Cross-references

- [../structs/cTribe.md](../structs/cTribe.md) — `resource[5]` is the five mana
  spheres; `g_TribeResourceInit`/`Max` are `START_MANA`/`MAX_MANA`.
- [population-and-births.md](population-and-births.md) — the man type byte
  `+0xb3`, type `0x19` = child, and the config-var resolution method.
- [calendar.md](calendar.md) — `g_TicksPerDay` / `g_TicksPerMinute`, which set
  what "480 minutes" and "one day" mean.
- [dev-console.md](dev-console.md) — `mannaking <mana> [sphere]` names the five
  spheres and can set them directly.
