# The `.tsg` save format, and the bug that corrupts it

How Theocracy writes a save, why every campaign dies after ~50 saves, and how
the port fixes it. Addresses are **Ghidra space** (game base `0x08048000`).

## The save path

```
FUN_081a0a10   SaveGame(path)          puts("Saving Game")
  ├─ Write(file, local_ac, 0x48)       72-byte header
  └─ FUN_0817b830   cGameSession::Save
       ├─ +0x4c (4), +0x2d (1)         local faction
       ├─ 11 × FUN_08159c40            cTribe::Save + one faction byte each
       ├─ +0x51 (4)
       ├─ world → vtable+0x10          → FUN_082259f0 → FUN_081fb920 cWorld::Save
       │                                  └─ FUN_0818d450: two polymorphic arrays
       │                                     (world+0x88 count +0x90,
       │                                      world+0x9c count +0xa4), each
       │                                     element through vtable+8
       └─ 11 × FUN_08159d80            → FUN_080504a0 → FUN_08053410 / FUN_081476b0
```

`FUN_08196ba0` is the shared string writer — `[u32 len][bytes + NUL]`, which is
what puts `cProvince` and the map path into the file.

## The header is 54 bytes of uninitialised stack

```c
char local_ac[64];  undefined1 local_6c[8];   // contiguous on the stack
strcpy(local_ac, local_64);                   // ~10-char date, e.g. "1429/6/18"
                                              // see calendar.md for the date rule
Write__5cFilePCvUl(&file, local_ac, 0x48);    // writes all 72
```

`local_ac` only ever receives the date string; the remaining 54 bytes are never
initialised and are written to disk anyway. Every save leaks stack. In real
files those bytes are **live guest pointers** — measured across three saves of
the same state, the differing header fields were heap addresses at `+0x10`,
`+0x14`, `+0x2c`, `+0x38` and a stack fragment at `+0x08`, alongside stable
libmvos pointers at `+0x0c`/`+0x34` (stable only because that image loads at a
fixed base).

Harmless in practice — the loader does not read them — but it is why two saves
of an identical game state never compare equal, and it would be a privacy leak
in any program that mattered. A `memset(local_ac, 0, 64)` would fix it.

## The corruption bug

Every save **appends a byte-identical copy** of a small group of records to a
list at the end of *each province's* data. Each list stores its length in a
**single byte** counting 17-byte units:

| | |
|---|---|
| unit | **17 bytes** — four LE `u32` then one trailing byte |
| group appended per save | **4 units (68 B)**; map23 appends **5 (85 B)** |
| counter | **1 byte**, immediately before the list, = number of units |
| lists per file | **44** (one per province) |

So the counter climbs 4 or 5 per save and dies at 255 — the 5-per-save province
first, at **51 saves**. Past that the byte wraps, the loader reads the wrong
length, and every byte after it is misparsed. That is the "save corrupts after
~50 saves" bug.

The engine writes byte-sized list counts as a matter of course. `FUN_08053410`
is the same shape on a different class, confirmed against instructions rather
than the decompiler (the offset comes off an `int *`, so it is exactly the
scaling trap in [../reference/re-methodology.md](../reference/re-methodology.md) §2):

```
08053421: PUSH 0x1               ; one byte
08053423: LEA  EAX,[EBX + 0x1c]  ; count at object +0x1c
0805342b: CALL EAX               ; Write(file, obj+0x1c, 1)
                                 ; then walk the list, write each element
```

**The appended groups carry nothing.** They are exact duplicates, which is why
hand-deleting all but one recovers a file — long-known to the community, at a
cost of 6–12 hours of hex editing per save.

### Measured, from three real saves

Saves of the same game state at different save counts:

| | save0 | save1 | save2 |
|---|---|---|---|
| groups per list | 2 | 2 | 6 |
| counter byte | 8 | 8 | 24 |
| file size | 591,687 | 591,687 | 603,723 |

save2 is four saves further on: 4 groups × 68 B × 44 lists = 11,968, plus the
one 5-unit province's extra = **12,036 bytes**, exactly the observed growth. All
three collapse to **the same 588,678 bytes**, and the size removed —
`43 × 68 + 1 × 85 = 3,009` — independently confirms 43 four-unit provinces plus
map23.

## The fix

Collapse every run to one group and reset the counter, at the file boundary the
host already owns. `TrapLayer::collapse_save_file` runs when the game closes a
`.tsg` it wrote; `tools/fix_save.py` is the same algorithm offline, and is what
it was developed and cross-checked against.

**Why not patch the game.** The write site sits behind two layers of virtual
dispatch (`vtable+8`, `vtable+0x28`) and was not pinned; byte-patching logic
that is not fully understood is the riskier change. The file boundary is a
place we already control, where the whole artefact is present and consistent,
and where the result can be verified against real saves.

**Anchored on the counter, never on the repetition.** This is the part that
matters. Once a list holds many identical groups, the periodicity *also* holds
at offsets inside a group, so scanning for the repeat can lock onto a shifted
phase and write the counter over a data byte. The first cut did exactly that
and corrupted 86 bytes of a 64-group file. Reading each byte as a candidate
count instead makes the phase exact: the count claims a layout, and the layout
is then verified against the bytes.

**It refuses anything it does not fully recognise.** Every province gets one
group per save, so an intact file has the *same* group count in every list.
Disagreement means the parse is wrong. On an already-overflowed file the
counter-anchored scan finds 125 "lists" with 2–38 groups — because inside a long
run there are interior bytes that describe a plausible sub-run — and all three
guards fire. Nothing is written.

**Already-overflowed saves are detected, not repaired.** The counter is the only
thing that fixes the phase, and once it has wrapped there is nothing
trustworthy to anchor to; guessing yields a file that will not load. The tool
reports the runs and stops. Running the fix *before* the counter overflows is
what makes this a non-issue, and with the host hook active it never gets close
— the counter is reset to 4 on every save.

### Verified

**End-to-end, in the game (2026-08-02).** A bloated save (save2, 6 groups per
list, 603,723 B) was loaded, played and saved to a new slot. The hook fired on
close: **588,678 bytes, 0 collapsible runs left** — byte-for-byte the size the
offline tool predicted, with every list back to one group. The result differs
from the prediction by 4,893 bytes (0.83%), the same magnitude as ordinary
save-to-save variance (4,894 between two saves of the same state), i.e. real
game state and the uninitialised header, not structure. **The collapsed save
then loaded correctly**, which is the direction that actually proves one group
is sufficient. The round trip is closed.

Also:

- Three real saves and one synthetic 64-save overflow.
- Healthy files: lossless (repaired save2 differs from repaired save1 by 4,891
  bytes, against 4,894 for the originals — only duplicates removed), and
  **idempotent** (a second pass finds nothing).
- Overflowed file: refused by both implementations, left byte-identical.
- **C++ and Python agree byte-for-byte on all four**, which is the check that
  makes the in-game path trustworthy — it is exercised by
  `THEOC_FIX_SAVE=<path>`, which needs no display and no data tree.

## Knobs

| Variable | Effect |
|---|---|
| `THEOC_FIX_SAVE=<path>` | repair one `.tsg` and exit, without booting |
| `THEOC_NO_SAVE_FIX=1` | leave saves alone on the in-game save path |

## Open

- **The append site.** What adds a group per save is still unfound; it is behind
  the polymorphic element writers above. Finding it would allow fixing the cause
  instead of the artefact. The cheap route is dynamic, not static: log every
  guest write with `(file offset, length, caller EIP)` during one save and look
  up which code writes the counter byte — the file route was tried at length and
  branched faster than it converged.
- **What the 17-byte units mean.** Four `u32` and a byte; observed values look
  like coordinate/id pairs. Not needed for the fix, since the duplicates are
  provably redundant.
