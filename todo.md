# todo

Open tasks only. A task leaves this file when it is done — the finding it
produced goes into `docs/`, which stays the record. Nothing here is a plan or a
rationale; if it needs either, it belongs in a doc that this file links to.

Two sections because the split matters: **YOU** is work that needs a machine,
hardware or a judgement only you have. **CLAUDE** is work I can do unattended.

---

## YOU

### 1. Windows on bare metal — blocked until ~2026-08-18

Hardware arrives via a third party around then. When it does, on that machine:

```
win-timing-probe.exe
win-timing-probe.exe --busy <core count>
win-timing-probe.exe --busy <core count x 2>
```

plus one ordinary game session with `THEOC_FPS=1`.

Two specific things only bare metal answers, both recorded in
[`docs/porting/other-os-ports.md`](docs/porting/other-os-ports.md), "The
contention runs": whether the ~98 ms loaded province frame is a VM artefact, and
what happens on a machine where nothing has already raised the global timer
resolution to 1 ms (the VM's was already raised before the probe ran).

---

## CLAUDE

### 1. Magic items — and the ones with no flavour text

Several items ship with no lore text. Decide, per item, which of three it is:
cut content, a text lookup keyed differently, or a gap in one language's files.
**Only the effect handler answers that** — a `case` for the item means it works
and nobody wrote the lore; falling through a default means dead content.

**The entry points are already found** — the hero work landed on them, so this
starts from code, not from a search
([`docs/subsystems/heroes.md`](docs/subsystems/heroes.md)):

- **`0x0820d1f0` is the item factory.** A switch over ids `1..0x32` — so there
  are **exactly 50 magic items** — each `new`ing an object and calling its own
  constructor. Ids 2, 8 and 50 allocate `0x1c` bytes where every other item takes
  `0x18`: three items carry an extra field, and that is where to start.
- `data/mitem.cfg` is loaded at `0x081fb5b0`, two lines before `data/hero.cfg`.
- `FUN_080a8120(man, item)` is the give-item call.
- Text keys follow the `manname_`/`mandesc_` pattern; the locale `.sdb` format is
  cracked and written up in
  [`docs/reference/phls-format.md`](docs/reference/phls-format.md).

Read the 50 constructors and classify each item: does it write anything, and is
there a `mitemdesc_*` (or equivalent) entry for it? An item that constructs but
writes nothing is the same finding as Umochi.

Balance numbers reach the code the usual way — `selap.txt` key →
`LoadConfigVar` → global → xref
([`docs/subsystems/population-and-births.md`](docs/subsystems/population-and-births.md)).
**Decrypt before grepping** — [`re-methodology.md`](docs/reference/re-methodology.md)
§13, which exists because this task's sibling got that wrong.

Precedent for the expected shape of the finding: HOSPITAL1 buys nothing,
HOSPITAL3 is identical to HOSPITAL2, CD track 4 is unreferenced, six `TEAMREG`
keys are dead. Content that exists in a table and does nothing in the code is a
house style here.
