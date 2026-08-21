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

### 2. Play one signed macOS bundle before publishing a release — small

The macOS CI job proves the bundle builds, relocates, signs, notarises and
loads. It cannot prove the guest runs: `THEOC_FIX_SAVE` returns from `main`
before Unicorn is opened, so the hardened runtime's effect on the JIT is
untested by anything CI does. It was measured by hand
([`other-os-ports.md`](docs/porting/other-os-ports.md), "The hardened runtime
versus Unicorn's JIT") and `port/theoc.entitlements` is the fix, but the first
Developer-ID-signed bundle should get one real session before the draft release
is published:

```
tar xzf theoc-macos-arm64-<version>.tar.gz
cd theoc-macos-arm64-<version>
# put data/cd/linux/ and data/game/ beside ./theoc, then
./theoc
```

If it dies with `Could not allocate dynamic translator buffer`, the entitlements
did not survive signing. Anything else is an ordinary bug.

### 3. Exercise the release job with three artefacts

It has only ever run with two. Push a throwaway `v*-rcN` tag and check the draft
release carries all four tarballs — Linux amd64, Linux arm64, macOS arm64,
Windows x64 — then delete the draft. The `macos` job also refuses to build
unsigned on a tag, so this is the first run that proves the secrets work.

---

## CLAUDE

**Refilled 2026-08-20, trimmed 2026-08-21** — all three packaging scripts and
all three CI jobs are done. Reasoning lives in
[`other-os-ports.md`](docs/porting/other-os-ports.md), "CI: building the bundles
on GitHub".

### 1. `THIRD-PARTY.md`

Decision 5: generated in CI where possible so it cannot drift from what shipped.
The Linux bundle ships **Debian's** `libunicorn.so.2`, so the
corresponding-source obligation attaches to that binary and the exact package
versions need recording at package time, not guessing afterwards.

---

### Historical note — why the bar was set high

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
