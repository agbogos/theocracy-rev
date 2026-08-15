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

**Empty again — 2026-08-15.** It was emptied deliberately on 2026-08-10, took
one task on 2026-08-15 when `reconf` turned up, and that task is done: the
engine's config vocabulary is read, `data/game/mvos.cfg` is corrected, and two
docs that had it wrong are fixed. What is left needs your machine, not mine.

The note below is why the bar is set where it is.

The port has been a release candidate since 2026-08-04 and nothing here
changes it. The RE side reached the point where every remaining question was
archaeology with no consumer: four sessions on 2026-08-10 closed four tasks and
opened four more, leaving the list exactly the size it started. That is what
reverse-engineering does — every answer exposes two things you now realise you
cannot explain — so the list does not converge, and the only way it ends is by
deciding it has.

What was here has **not** been thrown away. Unknowns belong in each doc's own
**"Open threads"** section, which is where `CLAUDE.md` says they live and where a
reader meets them in context instead of as a stray line in a task file:

| what | now in |
|---|---|
| Which men in `init.dat` carry a mission flag; the eight scenario `iMissionHandler` subclasses; `prov+0x400fb`; mission `+0x39` | [`missions.md`](docs/subsystems/missions.md) |
| Bone Horn's slot-5 body; item slots 5–7; items 30 and 40 | [`magic-items.md`](docs/subsystems/magic-items.md) |
| The regen and `*_HIT_PERCENT` keys; `cMan_Comm1`'s subtype byte; where a spell's `+0x350` school is set | [`heroes.md`](docs/subsystems/heroes.md) |
| The unit AI/movement core — the one genuinely large unread mass | [`simulation-step.md`](docs/subsystems/simulation-step.md) |

Refill this section when something has a *reason* — a bug, a port change, a
question you actually want answered — not to keep a queue non-empty.
