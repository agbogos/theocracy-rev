# The in-game calendar

Theocracy's calendar is not the Gregorian one. Months are 20 days, the year is
365, and a date is one count of days since Year 0 — held in memory as three
ints and written to a save as a single 4-byte integer.

**Verified against `theocracy.real` 2026-08-05.** Addresses are Ghidra space,
game base `0x08048000`. The rule was originally worked out by hex-editing saves
before the port existed; every part of it has now been read off the code, one
part of it was wrong, and the two things the hex-editing method could not see
are settled below.

## The class

A small `cDate`-shaped object — three ints and a vtable pointer:

| offset | field |
|---|---|
| `+0x00` | year |
| `+0x04` | month index (**0-based**) |
| `+0x08` | day index (**0-based**) |
| `+0x14` | vtable (`0x08378ba4`) |

The live game date is the instance at **`g_World + 0x83c`** — that is the object
the console writes, the save serialises and the UI formats.

| Address | Named | Does |
|---|---|---|
| `0x081a2000` | `cDate_SetFromDayCount` | day count → the three fields |
| `0x081a2060` | `cDate_ToDayCount` | the three fields → day count |
| `0x081a1fa0` | `cDate_ctor_YMD` | construct from year/month/day |
| `0x081a20b0` | `cDate_Clear` | zero all three |
| `0x081a1e90` | `cDate_Save` | write 4 bytes |
| `0x081a1ec0` | `cDate_Load` | read 4 bytes |
| `0x081a2290` | `cDate_ToString` | format for display |

(Names are ours, written back into the Ghidra DB; the binary is
`.symtab`-stripped here.)

## Where 20 and 365 come from

`InitTimeUnitConstants` (`0x080b7b00`) builds every duration as
*seconds × `g_TimeUnitScale`*, the scale being one global read twice at
`0x080b7b05`/`0x080b7b0d`. Read off the instructions rather than the
decompiler, since these are the load-bearing values:

| Global | Value | = |
|---|---|---|
| `0x085a5874`, `0x085a588c` | 60 k | minute |
| `0x085a5888` | 3,600 k | hour |
| **`0x085a5884`** (`g_TicksPerDay`) | **86,400 k** | day |
| `0x085a5880` | 604,800 k | week (7 days) |
| **`0x085a587c`** (`g_TicksPerMonth`) | **1,728,000 k** | **20 days** |
| **`0x085a5878`** (`g_TicksPerYear`) | **31,536,000 k** | **365 days** |

The calendar code never uses these directly — only the two ratios
`g_TicksPerMonth / g_TicksPerDay` = **20** and `g_TicksPerYear / g_TicksPerDay`
= **365**, so the scale cancels and the calendar is exact whatever it is set to.
The week constant exists and the date code never touches it.

## The rule, as the code states it

Encode — `cDate_ToDayCount`, with both indices 0-based:

```c
count = 365 * year + 20 * monthIndex + dayIndex;
```

Decode — `cDate_SetFromDayCount`:

```c
year       = count / 365;
rem        = count % 365;
monthIndex = rem / 20;
dayIndex   = rem % 20;
```

**The epoch is confirmed**: year 0, month index 0, day index 0 encodes to 0.
There is no offset constant anywhere in either direction — the round trip is
pure div/mod, so an epoch that was secretly shifted would have to show up here,
and does not.

## The five days that have no month

365 = 18 × 20 + **5**, and this is the question the hex-editing session could
not answer, because it would have had to land on one of 5 days in every 365 to
notice. The answer is that **the engine does not handle them at all**:
`cDate_SetFromDayCount` is plain div/mod with no special case, so days 360–364
of each year decode to **month index 18**, day index 0–4.

`cDate_ToString` then prints `monthIndex + 1` through `%02d` — a number, not a
lookup into a table of 18 month names — so nothing over-reads and nothing
crashes. Those five days simply display as **month 19**, a short month at the
end of every year. "18 months plus 5 intercalary days" describes the arithmetic
correctly and the engine's own presentation incorrectly.

## How it is stored

`cDate_Save` encodes and writes **one 4-byte little-endian int**:

```c
int count = cDate_ToDayCount(this);
file->Write(&count, 4);
```

`cDate_Load` is the exact inverse — read 4, decode. So the field is a plain
`int`, and the old notes' "3 bytes" was the low three bytes of it: the top byte
stays zero until year ~45,965, and would never have been seen moving.

**There is no fixed file offset.** `cWorld::Save` (`0x081fb920`) calls
`cDate_Save` partway through its stream, after two polymorphic arrays and a
length-prefixed string, all of which vary in size — so the date's position in a
`.tsg` moves from save to save. That is why the hex-editing notes never recorded
an offset: there isn't one to record. In the stream it sits between the world's
`+0x5c0`/`+0x5c4`/`+0x5c5`/`+0x5c6` fields and the 4 bytes from `+0x1408`.

| Date | count | as stored |
|---|---|---|
| Year 0, Month 1, Day 1 | 0 | `00 00 00 00` |
| Year 1, Month 1, Day 1 | 365 | `6D 01 00 00` |
| Year 1429, Month 6, Day 18 | 521,702 | `E6 F5 07 00` |
| Year 1429, Month 19, Day 5 | 521,949 | `DD F6 07 00` |

## On screen, and in the save header

`cDate_ToString` is one `sprintf` into a single static buffer at `0x086481e0`:

```c
sprintf(buf, "%04d/%02d/%02d", year, monthIndex + 1, dayIndex + 1);
```

Always exactly 10 characters plus the NUL, zero-padded — so a date in the last
5 days of Year 1429 renders `1429/19/03`.

This is also the answer to how a date gets into the save header. The save/load
slot dialog (`0x0819ea00`) seeds the filename edit box with
`cDate_ToString(g_World + 0x83c)`, and whatever the box ends up containing is
handed back to `SaveGame` and `strcpy`'d into the 72-byte header — which is why
a real save header opens with something like `1429/06/18`. So the header text
and the binary count do share one source object, but only by default: the
header holds a **save name** that starts life as the formatted date and can be
typed over. See [save-format.md](save-format.md).

## Setting a date from the console

`date <year> <month> <day>`, handled in the realm console dispatcher at
`0x081f3410` and gated on edit mode like the rest of the editor commands
([dev-console.md](dev-console.md)). It takes exactly three arguments — fewer
prints the syntax line — parses each with `sscanf("%d")`, defaults to
**1999/7/1** if a field fails to parse, clamps negatives to zero, then subtracts
one from month and day and calls `cDate_SetFromDayCount`. Nothing is clamped at
the top: `date 5 25 40` is accepted and normalises through the div/mod.

`THEOC_CONSOLE=1` opens the console without patching the game.

## Elsewhere in the same units

- **Scenario data** — `SCN_3T01_STRIKEDELAY_DAY=175`, `SCN_5T01_TIMEOUT_YEAR=10`
  drive realm-screen world events
  ([../porting/frame-timing.md](../porting/frame-timing.md)). 175 days is
  8 months and 15 days under this rule.
- **`SimulationStep`** reads `g_TicksPerDay` at `0x081f96a3` and calls
  `cDate_ToDayCount` at `0x081f9511` and `0x081f967a` — the advance path, not
  chased here.

## Open threads

- **How the date advances.** The three `SimulationStep` sites above are where a
  tick becomes a day. Worth reading if the port ever needs to drive the calendar
  itself rather than let the guest do it.
- **`g_TimeUnitScale`'s value** (`0x084c7c54`) was not read — the ratios above
  are independent of it, and hold as long as `31,536,000 × scale` does not
  overflow a 32-bit int (scale ≤ 68). Real saves decode to sane dates, so it
  does not, but the number itself is unconfirmed.
- **Whether month 19 means anything to the game.** It is five real days that the
  UI labels oddly; whether any scenario event or growth rule is keyed to a month
  number, and so behaves strangely in it, is unexamined.
