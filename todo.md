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

### 2. Optional: TOC of the other releases — not blocking anything

The UK rip works and the port ships one track mapping. Unknown: whether that
mapping holds for a *different* release, i.e. whether a tester with the Chinese
or prototype disc gets the right music or nonsense. The Chinese `.mds` sidecar
carries a full TOC and needs no drive at all, so this is desk work whenever you
feel like it. Agreement means the numbering is release-invariant; disagreement
is a finding and the port would have to detect rather than assume.
[`music-and-redbook.md`](docs/subsystems/music-and-redbook.md)

---

## CLAUDE

Nothing queued. The music subsystem closed out on 2026-08-08 — what is left of
it is genuine open threads in its own doc rather than tasks
([`music-and-redbook.md`](docs/subsystems/music-and-redbook.md)), and the one
*direction* it produced is the named first candidate in
[`native-rewrite.md`](docs/porting/native-rewrite.md).
