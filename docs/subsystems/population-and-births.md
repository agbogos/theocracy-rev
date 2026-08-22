# Province population and births

How Theocracy decides when a province produces a child, what hospitals actually
do about it, and why the birth rate does not taper off as a province fills up —
it stops dead.

**Read off `theocracy.real` 2026-08-08.** Addresses are Ghidra space, game base
`0x08048000`. The load-bearing claim here — the population cliff — is confirmed
against the instruction stream at both sites, not just the decompiler.

## The two functions

| Address | Name | Does |
|---|---|---|
| `0x081db7e0` | **`cProvince::EatHealBirth`** | the mechanic: eating, healing and births, once per province per elapsed day |
| `0x081db530` | **`cProvince::UpdateBirthRate`** | recomputes the derived per-year figure at `+0x40f74` and nothing else |

The first names itself — `Fatal("cProvince::EatHealBirth : Negative resource
amount")`. The second is ours, and is named for the only thing it writes.

Both take a `cProvince*`; `+0x40aae` (the owner byte from
[simulation-step.md](simulation-step.md)) is the anchor that identifies the
parameter. The two functions compute the same clamp and the same formula
independently, which is what makes the offsets below trustworthy: `EatHealBirth`
decompiles with an `int *` parameter and so prints **scaled** indices
(`param_1[0x103dd]`), while `UpdateBirthRate` uses a plain `int` and prints byte
offsets (`+0x40f74`). `0x103dd × 4 = 0x40f74`. Two independent derivations of
the same field is the check [re-methodology.md](../reference/re-methodology.md)
§2 asks for, and they agree on every offset used here.

## The mechanic

```c
P = eligible people in the province          // all 11 tribe slots, see below
D = FUCK_PER_BIRTH (7120)  or  FUCK_PER_BIRTH_HOSP2 (5476)
elapsed = (today - lastDay) * g_TicksPerDay  // province[+0x40f6c] holds lastDay

province[+0x40f70] += P * elapsed;                        // the accumulator
while (province[+0x40f70] >= D * g_TicksPerDay) {
    spawn one man of type 0x19, tribe = province[+0x40aae];
    province[+0x40f70] -= D * g_TicksPerDay;
}
```

`g_TicksPerDay` appears on both sides and cancels, so the accumulator is
**person-days** and `D` is **person-days per birth**. 7120 person-days buys one
child. A province holding `P` eligible people therefore produces a birth every
`D / P` days:

| P | no hospital / HOSPITAL1 | HOSPITAL2 / HOSPITAL3 |
|---|---|---|
| 100 | a birth every 71 days | every 55 days |
| 400 | every 17.8 days | every 13.7 days |

The `while` is a loop rather than an `if`, so a long stall that swallows many
days at once still pays out every birth it owes — the accumulator is the record,
not the frame.

`UpdateBirthRate` writes the same quantity as an annual figure:

```c
province[+0x40f74] = (D/2 + 365*P) / D;      // = round(365*P/D), unsigned
```

`365` is not a literal: it is `g_TicksPerYear / g_TicksPerDay`, the same ratio
the calendar uses ([calendar.md](calendar.md)), read at `0x081db79e`. The `D/2`
is round-half-up. Nothing in the birth path reads `+0x40f74`, so it is a derived
display/AI value; *that it is the number the province UI shows* is a strong
hypothesis, not something read — see Open threads.

## Who counts as eligible

Both functions apply the same filter while walking, for each of the **11 tribe
slots**, two per-tribe lists on the province:

- `+0x40b98 + tribe*0x30` — leaders, each of which is also walked for its
  inferiors at `leader+0x10`
- `+0x40bb0 + tribe*0x30` — a second flat list

A man counts when **all** of these hold:

| Test | Field |
|---|---|
| `> 0` | `man+0x60` |
| `!= 2` and `!= 10` | `man+0x11c` — a state enum; `10` is separately handled as a corpse-like case |
| `!= 5`, `!= 0x1c`, `!= 2` | `man+0xb3` — a type/profession byte |

The three excluded `+0xb3` values are not identified. `+0xb3 == 0x19` **is**
identified, indirectly and strongly: it is the type `EatHealBirth` spawns on a
birth, and it is counted separately against a 31% threshold that raises a
province notification — i.e. children, tracked so the game can warn you when too
few of them exist.

**Population of every tribe in the province counts**, not just the owner's. The
child is nevertheless spawned for the owner (`province[+0x40aae]`). So foreign
population inflates your birth rate — and can push you over the cliff below.

## Hospitals: a maximum, not a stack

Both functions walk the province's building list at `+0x40b34` and reduce it to
a single tier, taking the **highest** hospital present:

```
building type 0x0a  ->  tier 1     (guarded by `if (tier < 1)`)
building type 0x10  ->  tier 2     (guarded by `if (tier < 2)`)
building type 0x25  ->  tier 3     (guarded by `if (tier < 3)`)
```

Each guard only ever ratchets the tier upward, so **a second hospital of any
level contributes exactly nothing** — not additively, not multiplicatively. The
list walk is a max, and the tier is then used as an index into two small sets of
constants:

| Tier | Birth divisor `D` | Births/yr at P=400 | Heal per day |
|---|---|---|---|
| 0 — none | `FUCK_PER_BIRTH` 7120 | 21 | `NO_HOSPITAL_HEAL` 1 |
| 1 — HOSPITAL1 | **`FUCK_PER_BIRTH` 7120** | **21** | `HOSPITAL1_Heals` 2 |
| 2 — HOSPITAL2 | `FUCK_PER_BIRTH_HOSP2` 5476 | 27 | `HOSPITAL2_Heals` 4 |
| 3 — HOSPITAL3 | **`FUCK_PER_BIRTH_HOSP2` 5476** | **27** | `HOSPITAL3_Heals` 4 |

The identification of the tiers as the three hospitals rests on the *config
variables the tier selects* — `HOSPITAL1_Heals` / `HOSPITAL2_Heals` /
`HOSPITAL3_Heals` / `NO_HOSPITAL_HEAL`, chosen by tier 1 / 2 / 3 / 0
respectively — which is about as direct as naming gets. The building **type
ids** `0x0a`/`0x10`/`0x25` are not independently confirmed; the binary carries
no plain building-name table, only the `HOSPITALn_STONE`-style config keys.

Two balance consequences fall out of the table, and both look like oversights
rather than design:

- **HOSPITAL1 does nothing for births.** It selects the same divisor as having
  no hospital at all. Its only effect is doubling the heal, 1 → 2.
- **HOSPITAL3 is identical to HOSPITAL2** on both axes — same divisor, and
  `HOSPITAL3_Heals` and `HOSPITAL2_Heals` are both **4** in the shipped
  `selap.txt`. It costs 240 stone / 240 wood / 90 jewel against HOSPITAL2's 120
  / 180 / 0, and the only thing it buys is `ROOM_FOR_PEOPLE` 60 vs 30.

So the entire hospital contribution to population growth is a single flat
**+30%** (`7120/5476 = 1.30`), unlocked at level 2 and never improved.

## The cliff

This is the answer to "why does the birth rate crash instead of tapering". It is
neither an unsigned overflow nor a soft saturation — it is an explicit zero:

```asm
081db788  CMP EDI, [0x084c8599]            ; MAX_FUCKER = 500
081db78e  JBE 0x081db792
081db790  XOR EDI,EDI                      ; <-- eligible count = 0
081db792  MOV ECX, [0x084c859d]            ; MAX_EFFECTIVE_FUCKER = 400
081db798  CMP EDI,ECX
081db79a  JBE 0x081db79e
081db79c  MOV EDI,ECX                      ; eligible count = 400
```

`EatHealBirth` has the same pair at `0x081dc7f6`, spelled `MOV dword [ebp-0x9c],
0` instead of `XOR`. Both comparisons are unsigned (`JBE`). So, per province:

| Eligible population | Birth rate |
|---|---|
| P ≤ 400 | proportional to P |
| 400 < P ≤ 500 | flat, pinned at the P=400 rate |
| **P > 500** | **zero, immediately** |

The shape is worth naming: the *second* test is the ordinary clamp idiom (`if (x
> MAX) x = MAX`), and the first is that same idiom with `0` substituted for the
cap. Whether that was a deliberate overpopulation-collapse rule or a slip is not
readable from the code, and this doc does not claim to know. What is settled is
that the simulation and the derived per-year figure apply it identically, so the
number a player sees is not lying about the number of children they will get.

Note the compiled-in defaults are much gentler than the shipped balance — see
below — which means the cliff as *played* is a data decision, not a code one.

## The constants

All are `LoadConfigVar`-bound to `[Emulation]` and `[Food]` keys in
`data/selap.txt`. The `.data` values are compile-time fallbacks that the config
file overrides at boot.

| Key | Global | Shipped | Built-in default |
|---|---|---|---|
| `FUCK_PER_BIRTH` | `0x084c8591` | 7120 | 12000 |
| `FUCK_PER_BIRTH_HOSP2` | `0x084c8595` | 5476 | 10000 |
| `MAX_FUCKER` | `0x084c8599` | 500 | 1000 |
| `MAX_EFFECTIVE_FUCKER` | `0x084c859d` | 400 | 100 |
| `NO_HOSPITAL_HEAL` | `0x084c85a1` | 1 | 50 |
| `HOSPITAL1_Heals` | `0x084c8560` | 2 | — |
| `HOSPITAL2_Heals` | `0x084c856c` | 4 | — |
| `HOSPITAL3_Heals` | `0x084c857c` | 4 | — |
| `CORNSTRENGTH` | `0x084c821c` | 60 | — |
| `MEATSTRENGTH` | `0x084c8220` | 120 | — |
| `STARVINGHPPERDAY` | `0x084c8224` | 6 | — |

The developers' own names. `FUCK_PER_BIRTH` is theirs, not a gloss, and reading
it as "person-days of cohabitation per child" matches the arithmetic exactly.

## Eating, in the same function

Not the subject of this doc, but `EatHealBirth` does it in the same pass and the
constants above decode it. Each eligible man either eats or starves:

- Food level is the `short` at `man+0x84`. At zero, the man loses
  `STARVINGHPPERDAY` HP per elapsed day; otherwise he is healed by the tier's
  heal amount.
- A man eats when his food level is below `0x084c862d − max(CORNSTRENGTH,
  MEATSTRENGTH)`. A virtual at `man_vt+0x100` returns his preference: `0` corn,
  `1` meat, `2` "whichever the province has more of", compared as
  `stock*strength`. Eating consumes one unit from the province resource cache and
  adds that food's strength to his level.
- A second, lower threshold (a further `1.5 months` below) counts him as
  *hungry*, which drives the `HungryMan : %d` notification.

`0x084c862d` is the food-level ceiling and is **not** a registered config var —
17 read sites, no `LoadConfigVar` binding.

## How this was found

Worth recording because it generalises to any balance question about this game,
and it needed Ghidra only at the last step.

1. `selap.txt` is XOR-encrypted; `tools/theocracy_crypt.py` decrypts it. Grep the
   plaintext for the mechanic's vocabulary — here `birth`, `hospital`, `popul`.
2. Every key is bound to a global by `LoadConfigVar(&global, "NAME")` (`0x080b3de0`),
   which
   compiles to `push <name string>; push <global>; call`. Find the name string in
   `.rodata`, find the `push` of it in `.text`, and the next `push` is the
   global's address.
3. `tools/elfq.py xref-global <global>` gives the code that reads it.
4. Only then decompile — by which point the function is already known to be the
   one that matters.

Step 2 is the reusable key: it turns any balance constant a player can see in
`selap.txt` into an address, and therefore into the code that consumes it.

## Open threads

- **Is `+0x40f74` what the province UI displays?** It is the only per-year birth
  figure, and nothing in the birth path consumes it, but its four callers
  (`0x0807b951`, `0x0813bc10`, `0x0813bc4e`, `0x081c828c`) are unread. Until one
  is, "the displayed birth rate" is a hypothesis.
- **The eligibility enums.** `man+0x11c` (state, excluding `2` and `10`) and
  `man+0xb3` (type, excluding `5`, `0x1c`, `2`). Naming these would say exactly
  who breeds — and `+0xb3` is the same byte that identifies children as `0x19`.
- **Building type ids.** Confirm `0x0a`/`0x10`/`0x25` really are HOSPITAL1/2/3,
  which needs the building-type enum rather than the config keys.
- **Was the cliff intended?** `MAX_FUCKER` setting the count to `0` rather than
  clamping it. The 1.0 balance narrows the band to 400–500 where the built-in
  defaults left 100–1000, which at least suggests someone tuned around the
  behaviour rather than tripping over it.
- **`0x084c862d`**, the food ceiling — where it is initialised.

## Cross-references

- [simulation-step.md](simulation-step.md) — the province owner byte at
  `+0x40aae`, and the per-tick structure this hangs off.
- [calendar.md](calendar.md) — `g_TicksPerYear / g_TicksPerDay = 365`, and one
  tick = one in-game day, which is what makes `elapsed` a day count.
- [../reference/game-data-census.md](../reference/game-data-census.md) —
  `selap.txt` as the master balance file.
- [../reference/re-methodology.md](../reference/re-methodology.md) — §2 on the
  scaled-offset trap that the two functions cross-check each other against, and
  §8 on the incomplete-opcode-scan trap that this work found in `elfq.py` itself.
