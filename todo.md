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

### ~~1. Does the AI attack independent provinces?~~ — done 2026-08-10

Answered by play, no RE needed: **it does**. The assumption that it wouldn't
does not hold, so a generated world's ~60% grey map is not the dead end it
looked like. Written up in
[`starting-world.md`](docs/subsystems/starting-world.md), "Open threads".

### ~~2. Mission internals~~ — done 2026-08-10

All four bullets answered, in
[`docs/subsystems/missions.md`](docs/subsystems/missions.md): `iMissionHandler`
and its daily `Update`, the mission↔province switch, the four timers and the
Spanish invasion, the four unread missions, `Mission_FindManByFlag`, and province
virtual `+0xe0`. It also corrected the "units manager" at `g_World+0x1f394` in
three docs. What it left is below.

### 2. Mission internals — the residue

Ghidra: `theocracy.real`. All small; none blocks anything.

- **The eight campaign missions** (`cMission_S*_*`) still have unread bodies.
  Their lookup helper is read now, so the open part is **which bits mean what in
  `man+0x28`**, the mission-flag mask `Mission_FindManByFlag` tests. The `.man`
  files and `init.dat` men are where the bits are set.
- **The eight scenario `iMissionHandler` subclasses.** Only the campaign's six
  virtuals were read; the scenario ones are the eight remaining callers of
  `iMissionHandler_ctor` (`0x0820f420`). Cheap, and it would say whether the
  scenarios script anything or just hold missions.
- **`prov+0x400fb`** — the placement-mode byte behind province virtual `+0xe0`.
  No writer found; a `xref-global` will not help since it is an object field.
- **Mission field `+0x39`**, read by the campaign handler's `+0x1c` province
  predicate. No writer found.

### ~~3. Is `+0x27c` the hero id, or a general subtype byte?~~ — done 2026-08-10

Neither: `sizeof(cMan) == 0x27c`, so it is the first byte of the **derived**
class and `cHero` and `cMan_Comm1` each declare their own field there. Settled by
scanning `.text` for the displacement rather than by reading decompiles, which
also enumerated every use of the byte in the image. `heroes.md`'s claim needed no
scoping. Bonus: `HERO12_RANGE_MOD`'s consumer fell out of the same scan, so hero
abilities are now known to live in two places — baked in by `SetHeroId`, or
applied live in a per-class getter. See
[`heroes.md`](docs/subsystems/heroes.md), "What `+0x27c` actually is".

### ~~4. Smaller leftovers~~ — done 2026-08-10

- ~~The `+0x04` equip-restriction field on items~~ — a **bitmask** for the carry
  test (AND-ed against a per-man-class capability mask from man vtable `+0x24`)
  and an equality key for the duplicate test, with types `0x80` and `0x20` exempt
  from the latter. `cMan_TryEquipItemSlot` (`0x080a81f0`).
- ~~Who reads a man's `+0x88..0x90` magic-school slots~~ — `cMan_GetMagicResistance`
  (`0x080aded0`) and `cMan_ApplyMagicDamage` (`0x08098180`). It is one five-element
  `u16` array and a **percentage damage reduction**, not five flags. Four schools
  named from the spell classes that read their own slot; Moon by elimination.
- ~~The `+0x18` field on items 2, 8 and 50~~ — mechanism was already known;
  what is new is that **only id 2 initialises it**, so Moon Shield and Bone Horn
  read uninitialised heap. See [magic-items.md](docs/subsystems/magic-items.md),
  "Open threads".
- ~~`hero.cfg` columns 3 and 4 / province virtual `+0xe4`~~ — **done
  2026-08-09.** `+0xe4` is `(prov, pos, manType, tribe) -> cMan*`, and columns 3
  and 4 are the first eight bytes of the record, passed as that `pos`. See
  [missions.md](docs/subsystems/missions.md).

### 4. What this pass opened

- **Bone Horn (50)'s slot-5 body** — needed to say whether its uninitialised
  `+0x18` counter is observable in play, or harmlessly reset on first use.
- **Man vtable `+0x24`**, the per-class "which item types can I carry" mask. Read
  from its use in the equip checker, body unread; reading it would give the
  carrier table for all 50 items.
- **Spell `+0x350`** is the school id. Only its use is read; where a spell's
  school is set was not chased, and doing so would turn the Moon slot from
  elimination into a direct reading.
