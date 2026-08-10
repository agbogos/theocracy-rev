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

Nothing here gates anything. Ghidra: `theocracy.real` for all of it.

### 1. Which men in `init.dat` carry a mission flag

**The code half is done** (2026-08-10): `man+0x28` is written by nothing except
the two `cMan` constructors — zero, or four bytes straight from the world file —
so the flags are pure map-editor data, and only bits 0 and 1 are ever queried.
See [`missions.md`](docs/subsystems/missions.md), "Where the flags come from".

What is left is not a Ghidra task. Extend `THEOC_DUMP_WORLD` to print `man+0x28`
alongside the caste and run it over the nine world files, the same way
[`starting-world.md`](docs/subsystems/starting-world.md) read the hero and item
census. That answers which designer-placed men each campaign mission is looking
for, and it is the last piece the `cMission_S*_*` bodies need.

Needs a headless run, so it is a **YOU**-adjacent task in practice — the port
change is mine, the run is yours.

### 2. The eight scenario `iMissionHandler` subclasses

Only the campaign's six virtuals were read. The scenario ones are the eight
remaining callers of `iMissionHandler_ctor` (`0x0820f420`); two of them
(`0x0822ed30`, `0x08233210`) extend `Load` with a scenario-specific `u16`.
Cheap, and it would settle whether the scenarios script anything or just hold
missions. See [`missions.md`](docs/subsystems/missions.md), "What starts a
mission".

### 3. Smaller leftovers, worth doing only alongside something else

- **Bone Horn (50)'s slot-5 body** — whether its uninitialised `+0x18` counter
  is observable in play or harmlessly reset on first use
  ([`magic-items.md`](docs/subsystems/magic-items.md), "Open threads").
- **Where a spell's `+0x350` school is set.** Only its use is read. Chasing it
  would turn the Moon magic-school slot from elimination into a direct reading
  ([`heroes.md`](docs/subsystems/heroes.md), "The five magic schools").
- **`prov+0x400fb`**, the placement-mode byte behind province virtual `+0xe0`.
  No writer found, and `xref-global` cannot help — it is an object field, so it
  needs the [§17](docs/reference/re-methodology.md) displacement scan.
- **Mission field `+0x39`**, read by the campaign handler's `+0x1c` province
  predicate. No writer found.
