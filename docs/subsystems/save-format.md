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

## The header is a name, a window of uninitialised stack, and a magic

```c
char local_ac[64];  undefined1 local_6c[8];   // contiguous on the stack
strcpy(local_ac, local_64);                   // the save name, e.g. "1429/6/18"
Write__5cFilePCvUl(&file, local_ac, 0x48);    // writes all 72
```

Three regions, and an earlier version of this section got the third wrong by
implying the junk ran to the end of the 72:

| Offset | Bytes | What |
|---|---|---|
| `0x00` | 8–10 + NUL | the save name, NUL-terminated |
| ~`0x0a` | **53–55** | `local_ac`'s tail — **never initialised**, written anyway |
| `0x40` | 8 | `local_6c` = **`"theosg42"`**, the format magic. Real data |

The magic at `+0x40` is the same one every `init.dat` carries
([starting-world.md](starting-world.md)). It is inside the 72-byte write but it
is *initialised*, and anything editing this header must preserve it. So the
uninitialised region is bounded on both sides — not the tail of the header, a
hole in the middle of it.

The name **defaults to the in-game date**: the slot dialog seeds the filename
box from `cDate_ToString` and the player can type over it
([calendar.md](calendar.md)). Measured on real saves it renders **unpadded** —
`1429/6/18`, `1419/7/4`, `1330/14/4` — so 8 to 10 characters, not the fixed 10
an earlier reading of the format string claimed.

**The dialog caps a typed name at 40 characters** (measured: two 40-char saves
were made through the game and both stop exactly there). That number decides two
things. It puts the window between **23 and 55 bytes** rather than at a fixed
54 — the low end being what a stamp has to survive. And it means
`strcpy(local_ac, local_64)`, which has no bound check and would run a 64-byte
name straight into `local_6c` and destroy the magic, **cannot be reached from
the dialog**: 40 is comfortably clear of the 63 that would be needed. The
overflow exists in the code and is not reachable by a player.

Note also that the on-disk filename is *not* this string — saves are slot-named
(`save0.tsg` … `save9.tsg`) and this field is the player's label.

`local_64` is only filled on the interactive path — `SaveGame(path)` called with
an explicit path, as the console's `save` command does for `init.dat`, never
writes it, so **the whole 64-byte region is uninitialised** in that case.

In real files those bytes are **live guest pointers**. Across the same five
saves the window differs at `+0x0a`, `+0x10`, `+0x14`, `+0x2c`, `+0x30` and
`+0x38` — heap addresses in the `0x61xxxxxx` range — alongside libmvos pointers
at `+0x0c`/`+0x34` that look stable only because that image loads at a fixed
base.

The loader does not read the window, so this breaks nothing. It is still worth
fixing for a reason that costs real time: **two saves of an identical game state
never compare equal**, which is exactly the property you want when triaging a
save someone sent you. Before normalisation `save0` and `save1` — the same
state, saved twice — differed in **14 of 72 header bytes**; after, in none.

### What is written there instead

Having to choose the replacement bytes, the host writes the build identity
rather than zeros:

```
1429/6/18\08f4b26081263531dd+ma00\0\0…\0theosg42
└─ name ──┘└──── stamp, 22 ─────┘└ 0 ┘└─ magic ┘

1234567890123456789012345678901234567890\08f4b26081263531dd+ma00\0theosg42
└──────────── name, at the 40-char cap ─┘└──── stamp, 22 ─────┘└─ magic ┘
```

**22 fixed-width bytes, no separators**, so every field parses by offset:

| Field | Bytes | What |
|---|---|---|
| id | 4 | format id, a fixed constant |
| date | 6 | commit date, `YYMMdd` |
| sha | 7 | commit hash |
| flag | 1 | `+` dirty tree, `0` clean |
| host | 2 | host + arch: `ma` / `l6` / `la` / `w6` |
| knobs | 2 | which `THEOC_*` were in effect, as a hex bitmask |

Same reasoning as the startup banner
([../porting/diagnostics.md](../porting/diagnostics.md), "The first line names
the build"), applied to the other artefact a bug report arrives with: a
playtester sends a broken *save* far more often than a log, and a save that
names the build which wrote it answers "is this an old release?", "which host?"
and "what were they running with?" without an exchange of emails. The bytes were
being written regardless, so it costs nothing. `tools/fix_save.py header <file>`
decodes it.

**Sized against the floor, not the common case.** The window is 23–55 bytes, so
22 fits everywhere with a byte in hand — deliberately not 23, which would sit
exactly on a cap measured from two samples. **The stamp is never truncated:**
below its full width the window is zeroed, a fragment being neither parseable
nor recognisably absent. Swept across every reachable name length (0–40), the
output is always the complete stamp or nothing.

Three details that are choices rather than consequences:

- **The date is redundant against the hash, and kept anyway — precisely because
  it is.** The two fields have to agree, and making them agree requires the
  repository they came from, so an edited stamp does not survive inspection.
- **It is the *commit* date**, never the build date or the save's wall clock:
  either would make two saves of one state differ again, which is the whole
  point of the change.
- **The knob mask is read at stamp time, not at startup**, because `theoc.cfg`
  is loaded into the environment *after* `main()` hands the identity over — a
  knob set from the file has to count exactly as an exported one does. Eight
  bits, chosen for "could this explain a save that looks wrong?", so display and
  audio settings are absent: they cannot reach the file.
  `THEOC_NO_SAVE_FIX` is absent too, and could not be observed if it were — with
  it set, nothing is stamped at all.

The identity reaches `traps.cpp` through `TrapLayer::set_build_identity` rather
than a compile definition, so bumping the version does not rebuild the largest
translation unit in the port.

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

The same hook normalises the header, because it is the same file at the same
moment — but the two are kept **independent**. The header pass runs before the
run scan and on every save, including the ones with nothing to collapse and the
ones the collapser *refuses*: the two defects are unrelated and share only a
file boundary, so a save too damaged to restructure still gets a clean header.
It runs before the scan rather than after because the scanner reads from byte 0,
and normalising afterwards would let junk in the header change which runs are
found — enough for the C++ and Python implementations to disagree.
`THEOC_NO_SAVE_FIX=1` disables both, one knob for the whole hook, so that
"leave my saves alone" means it.

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

**The header pass, on the seven saves in `data/game/save` (2026-08-12).** Both
implementations run to the same bytes, which is the invariant that keeps the
in-game path checkable:

- **C++ and Python byte-identical on all seven**, across all three paths — three
  files collapsed *and* re-headered, four already-collapsed files that took the
  header-only path (`save3`, `save6`, `save7`, `save9`).
- **Both window extremes exercised by real saves.** `save6` and `save7` were
  made through the game with 40-character names — the dialog's cap — so their
  windows are 23 bytes against the other five's 53–55. All seven carry the same
  22-byte stamp.
- **No fragment is producible**: swept across every reachable name length
  (0–40), the window holds the complete stamp or nothing, magic intact in all 41.
- **The knob mask agrees across implementations**, which is the field most able
  to drift since it is read from the environment rather than baked in: with
  `THEOC_EDIT`, `THEOC_CONSOLE` and `THEOC_LONGRUN` exported, both sides write
  `0x85` and the decoder names all three back.
- **Idempotent**: a second pass over a normalised save writes nothing.
- **Reproducibility, which is the point**: `save0` and `save1` are the same game
  state saved twice. Their headers differed in **14 of 72 bytes** before and **0
  of 72** after.
- **The refusal path still refuses.** A file with one counter edited so its lists
  disagree (44 lists, 2 distinct group counts) is reported and its structure left
  alone — the only body byte that differs is the one deliberately corrupted —
  while the header is still normalised. That split is the behaviour the two
  passes are kept independent for.
- The magic at `+0x40` survives every path; `fix_save.py` and `traps.cpp` both
  refuse a file that does not carry it.

## Knobs

| Variable | Effect |
|---|---|
| `THEOC_FIX_SAVE=<path>` | repair one `.tsg` and exit, without booting — collapse *and* header |
| `THEOC_NO_SAVE_FIX=1` | leave saves alone on the in-game save path, both passes |

`tools/fix_save.py` additionally takes `header` (decode a save's name and
stamp), `--stamp` (match a specific binary exactly, for the cross-check) and
`--no-stamp` (zero the window without writing one).

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
