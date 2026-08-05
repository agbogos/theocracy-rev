# The in-game calendar

Theocracy's calendar is not the Gregorian one. Months are 20 days, the year has
18 of them plus 5 days left over, and a date is stored as a single count of days
since an epoch. This is the rule, both conversions, and what does not yet add
up.

**Status: derived from hex-editing saves, not from the disassembly.** It was
established before the port existed, by writing dates into `.tsg` files and
reading them back off the game's own display — so the rule is known to produce
the dates it is asked for. Nothing here has been checked against the code that
implements it, and the three things that method cannot see are stated in "What
does not add up" rather than papered over. Verification routes are in "Open
threads". Written up 2026-08-05.

## The rule

| | |
|---|---|
| month | **20 days** |
| year | **18 months + 5 days = 365 days** |
| epoch | Year 0, Month 1, Day 1 = **0** |
| stored as | days since epoch, **little-endian**, observed as **3 bytes** |

Months and days are 1-based in the arithmetic below and 0-based internally —
Month 1 is index 0, Day 1 is index 0 — which is where the `- 1` terms come from.
Years are not offset: year 0 is the epoch year.

## Date → stored value

```text
total_days = (year × 365) + ((month - 1) × 20) + (day - 1)
```

then write `total_days` little-endian.

| Date | `total_days` | value | bytes as stored |
|---|---|---|---|
| Year 0, Month 1, Day 1 | 0 | `0x000000` | `00 00 00` |
| Year 0, Month 1, Day 2 | 1 | `0x000001` | `01 00 00` |
| Year 1, Month 1, Day 1 | 365 | `0x00016D` | `6D 01 00` |
| Year 1429, Month 6, Day 18 | 521,702 | `0x07F5E6` | `E6 F5 07` |

The value column is the integer; the bytes column is that integer as it appears
in the file. Year 1429 is where the campaign save that was measured sits — the
first three rows are the epoch and the year boundary, which are the two places
an off-by-one would show.

## Stored value → date

```text
year           = total_days // 365
remaining_days = total_days %  365
month          = (remaining_days // 20) + 1
day            = (remaining_days %  20) + 1
```

`E6 F5 07` → `0x07F5E6` → 521,702 → 521,702 // 365 = 1429, remainder 117 →
117 // 20 + 1 = month 6, 117 % 20 + 1 = day 18. Year 1429, Month 6, Day 18.

## Where dates show up elsewhere

Three other places in this repo touch the same units, and none of them
contradicts the rule:

- **The save header carries the date as text.** The 72-byte `.tsg` header opens
  with a short date string — `"1429/6/18"` in the file that was measured
  ([save-format.md](save-format.md), "The header is 54 bytes of uninitialised
  stack"). Day 18 and month 6 both fit inside a 20-day month and an 18-month
  year, which is consistent with the rule without proving it; a Gregorian date
  would look the same. It does establish that the date exists in the file twice,
  once as text and once as the binary count, and only the second one is what a
  hex edit changes.
- **The dev console can set a date directly** — `date <year> <month> <day>`, in
  the editor command set ([dev-console.md](dev-console.md)). Three separate
  fields, matching the triple above.
- **Scenario data is written in the same units** — `SCN_3T01_STRIKEDELAY_DAY=175`
  and `SCN_5T01_TIMEOUT_YEAR=10` drive realm-screen world events
  ([../porting/frame-timing.md](../porting/frame-timing.md)). 175 days is 8
  months and 15 days under this rule.

## What does not add up

Three things, all of which the verification pass should settle.

**The 5 leftover days have no month, but the formula gives them one.** "18
months plus 5 days" and `month = remaining_days // 20 + 1` are not the same
statement: for the last five days of a year the formula returns **month 19**,
day 1 to 5. So either those days really are a short 19th month and the "5
intercalary days" phrasing is about how the UI presents them, or the engine
handles them some other way and the formula is wrong for 5 days in every 365.
The hex-editing session would very likely never have landed on one of those
days, which is exactly why the ambiguity survived.

**The field is probably not 3 bytes.** Three bytes is what was seen changing. A
26-year-old C++ engine storing a day count almost certainly uses a 4-byte `int`,
whose top byte would sit at zero for the ~46,000 in-game years a 3-byte field
spans and so would never have been observed moving. Treat "3 bytes" as the
observed width, not the declared one. The stated 16,777,215-day ceiling follows
from the observation and inherits its uncertainty.

**Where the field sits is unknown.** The rule says how to compute the value, not
where in the `.tsg` it goes — it was located by inspection each time and the
offset never written down. Until it is, the recipe below cannot be followed
blind, which is why finding the field is the first task in "Open threads".

## Editing a date by hand

Still the only way to change a date without the dev console. Back the file up
first — this is the sort of edit that is easy to get one byte wrong.

- `hexedit` is in Woody and is enough; `ghex` if X is up, `xxd`/`hexdump` for
  inspection or scripting.
- Compute the value with the formula above, write it little-endian over the
  existing bytes, keep the length identical.
- Verify by loading the save and reading the date off the game's display. That
  round trip is what the rule was established by, and it needs no tooling.

Everything else about `.tsg` files — the layout, the one-byte counters that
overflow after 51 saves, and the repair — is in
[save-format.md](save-format.md).

## Open threads

- **Find the field.** Its offset in the `.tsg`, its real width, and whether the
  header's text date and the binary count are written from the same source.
- **Settle the intercalary five.** Read the conversion in the code rather than
  inferring it. The `date <year> <month> <day>` console handler is the likely
  place — it has to do the same conversion in the same direction — and
  `THEOC_CONSOLE=1` opens the console without patching the game, so the black-box
  version of the test is available too: set a date in the last five days of a
  year and see what comes back.
- **Confirm the epoch.** Year 0 Month 1 Day 1 = 0 was assumed and then found to
  work; it has never been read off the code, and an epoch that is off by a
  constant would be invisible to a round trip that only ever edits and reads
  back through the same rule.
