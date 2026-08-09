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

### 1. Does the AI attack independent provinces?

Not campaign-gen archaeology — a live gameplay question that the generated world
raises and the shipped one hides. Shipped: every AI tribe already holds all it
will ever hold, greys untouched. Generated: ~60% of the map is unclaimed, so an
AI that ignores independents has opponents that can never grow.

**Try it in-game first, there may be no RE to do.** The console's `aiprov`
prints, for the province under the pointer, each tribe's capital distance,
optimal force and actual force. All-zero optimal force on a grey province
answers it:

```sh
THEOC_NEW_WORLD=1 THEOC_CONSOLE=1 ./port/build/theoc   # Alt+V, hover a grey province, `aiprov`
```

If the AI *does* value them and still never moves, the target selection is the
next read: `FUN_0815b130(tribe, buf, province)` is the per-tribe valuation
`aiprov` prints, and whatever calls it is the AI's province chooser.

### 2. Mission internals — what the 2026-08-09 pass deliberately left

[`docs/subsystems/missions.md`](docs/subsystems/missions.md) answered the three
questions the previous task asked and stopped there. Ghidra: `theocracy.real`.

- **What starts a mission.** The doc reads the lifecycle from `Start` onward and
  never establishes who constructs a `cMission_*`, how one is bound to a province,
  or how the campaign drives them. `cMissionTimer` / `cMissionTimer_Spain0/1` /
  `cMissionTimer_Dragon0/1` and `"Initializing timer to (%s) for mission (%d)"`
  (`0x08212f83`) are the entry points. **New lead (2026-08-09):**
  `cMissionHandler_Load` (`0x0820fcd0`) reads a day count and then **four timers
  and twelve missions straight out of the world file**, each through its own
  vtable slot `+0xc` — so mission state is *serialised with the world*, which is
  a different answer from "constructed by the campaign". See
  [`starting-world.md`](docs/subsystems/starting-world.md).
- **The four unread named missions**: `cMission_HeavyArmory`,
  `cMission_MountainVillage`, `cMission_Josda_Pre`, `cMission_WallChecker`. Their
  vtables and slots are in the doc; only the bodies are missing. They were skipped
  because none places a hero or an item.
- **The eight campaign missions** (`cMission_S*_*`). Established: they share the
  base vtable and the `.man` machinery. `cMission_S4_0_Start` (`0x0822f260`) looks
  its commander and hero up by man type via `FUN_08211e10` instead of creating
  them — that helper, and the `flag:%d` REF-node scanning around it
  (`0x08211ead`–`0x082124aa`), is the next target.
- **Province virtual `+0xe0`** — the predicate choosing between the two placement
  paths in `MissionCfg_PlaceMen`. Named from its use, body unread.

### 3. Is `+0x27c` the hero id, or a general subtype byte?

`cMan_Comm1`'s constructor (`0x08245920`) writes `26` to `+0x27c`, and
`FUN_08246150` copies a man *type* (`+0xb3`) into the same byte. Note the
"sole writer" framing this task originally had was **wrong** — `cHero`'s stream
constructor writes the byte too, via `lea` rather than a store, which is how the
first scan missed it. Little rests on the answer, but [`heroes.md`](docs/subsystems/heroes.md)'s
headline claim is flagged as class-scoped until this is read. Cheap: read
`cMan_Comm1` and whoever calls `FUN_08246150`.

**Half-evidence already in hand (2026-08-09):** all 42 caste constructors were
swept for stream reads beyond the base `cMan` record, and **only castes 33, 34
and 35 read an extra byte** — the three hero man types. So on the *load* path
`+0x27c` is the hero id and nothing else writes it. That does not settle the
runtime writers, which is what this task is about.

### 4. Smaller leftovers, worth doing only alongside something else

- The `+0x04` equip-restriction field on items: the checker is unread, so
  bitmask-of-carriers vs. category id is unsettled
  ([magic-items.md](docs/subsystems/magic-items.md)).
- Who reads a man's `+0x88..0x90` magic-school slots. The offset→school mapping
  (Sun/Moon/Star/Nature/Soul) currently rests on hero description text agreeing
  across nine heroes, not on the consuming code
  ([heroes.md](docs/subsystems/heroes.md)).
- The `+0x18` field on items 2, 8 and 50 — three items allocate four extra bytes
  and only id 2's initialisation was observed.
- ~~`hero.cfg` columns 3 and 4 / province virtual `+0xe4`~~ — **done
  2026-08-09.** `+0xe4` is `(prov, pos, manType, tribe) -> cMan*`, and columns 3
  and 4 are the first eight bytes of the record, passed as that `pos`. See
  [missions.md](docs/subsystems/missions.md).
