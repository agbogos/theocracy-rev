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

### 1. Mission code — the one thread both finished tasks ran into

Heroes and magic items both ended at the same wall: content that is fully
implemented but placed by neither `hero.cfg` nor `mitem.cfg`. Reading the
mission code closes three questions at once.

- **Eight heroes are never placed** (ids 3, 5, 8, 9, 10, 11, 17, 19).
- **Ring Piece 1 and 2 (items 24, 25) are inert**, and the fully-implemented
  Ring of Concordance (26) appears in no data file. If the pieces combine, that
  step is in mission code.
- **Is Mask of the Brave (item 1) reachable at all?** It has no effect, no
  constants and no description. If no mission places it, it is dead in the
  shipped game rather than merely silent — which is a different and stronger
  finding than the one currently written down.

Entry point: `cMission_S4_0::Start` carries the assert `"Hero not found."`
(Hungarian, and rude about a colleague) — grep the binary for `cMission_` to get
the class list.

Findings go into the existing
[`docs/subsystems/heroes.md`](docs/subsystems/heroes.md) and
[`docs/subsystems/magic-items.md`](docs/subsystems/magic-items.md) "Open
threads", or a new mission doc if it turns out to be its own subsystem.

### 2. Smaller leftovers, worth doing only alongside something else

- The `+0x04` equip-restriction field on items: the checker is unread, so
  bitmask-of-carriers vs. category id is unsettled
  ([magic-items.md](docs/subsystems/magic-items.md)).
- Who reads a man's `+0x88..0x90` magic-school slots. The offset→school mapping
  currently rests on hero description text
  ([heroes.md](docs/subsystems/heroes.md)).
- The `+0x18` field on items 2, 8 and 50 — three items allocate four extra bytes
  and only id 2's initialisation was observed.
