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

### 1. Parse `init.dat` — what actually ships in the starting world

**The highest-value item here**, because three docs currently carry claims that
only this can settle. Established 2026-08-09: **every `init.dat` in the tree is a
`theosg42` savegame** — `data/campaign/init.dat` (550 831 bytes) and all eight
`data/scenario/scn*/init.dat` — so the starting state of every map is *loaded*,
not placed by code. Objects arrive through stream constructors:

- `cHero`'s is **`0x080b22c0`** — base `cMan` stream ctor, hero vtable, then one
  byte straight into `+0x27c`.
- The base `cMan` one is `0x080aeb10`, itself chaining to `0x08094730`.
- Items: `0x0820dbb0` reads an id byte and calls `Item_CreateById`, so a world
  file can materialise **any of the fifty**.
- Both are reached through the per-man-type **caste properties** struct, which
  stores the loader at `caste+0x48` (`FUN_08254570` writes
  `_DAT_0866d828 = FUN_082543b0` for man type `0x21`) — an indirect call with no
  rel32, which is why they were missed.

Walk the stream-ctor chain to get the `cMan` record layout, then answer:

1. **Is Jarakhi (hero 11) in `data/campaign/init.dat`?** The user reports he is
   the campaign's player character — neither spawned nor rewarded because he
   ships with the map. Confirming it byte-wise closes the loop.
2. **Is Tlechlal (19) baked in somewhere too?** Same audit; currently
   unexplained.
3. **Which of the 14 no-code-path items appear?** Especially **Mask of the Brave
   (1)** — "dead in the shipped game" is withdrawn until this is run, in
   [`magic-items.md`](docs/subsystems/magic-items.md) and
   [`missions.md`](docs/subsystems/missions.md).

Note `save-format.md` already recorded that the console's `save` command writes
`init.dat`; the fact was in the repo and went unconnected. Also worth testing:
`hero.cfg` loads only under `*(int *)(g_GameSession + 0x4c) == 0`, which now
reads as "new game, not a loaded world".

### 2. Mission internals — what the 2026-08-09 pass deliberately left

[`docs/subsystems/missions.md`](docs/subsystems/missions.md) answered the three
questions the previous task asked and stopped there. Ghidra: `theocracy.real`.

- **What starts a mission.** The doc reads the lifecycle from `Start` onward and
  never establishes who constructs a `cMission_*`, how one is bound to a province,
  or how the campaign drives them. `cMissionTimer` / `cMissionTimer_Spain0/1` /
  `cMissionTimer_Dragon0/1` and `"Initializing timer to (%s) for mission (%d)"`
  (`0x08212f83`) are the entry points.
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
first scan missed it (task 1). Little rests on the answer, but [`heroes.md`](docs/subsystems/heroes.md)'s
headline claim is flagged as class-scoped until this is read. Cheap: read
`cMan_Comm1` and whoever calls `FUN_08246150`.

### 4. Mission code — the wall that heroes and magic items both hit — DONE 2026-08-09

Kept for one commit as a record of what the task asked versus what it found;
delete on the next pass. All three questions answered in
[`docs/subsystems/missions.md`](docs/subsystems/missions.md): the ring pieces
combine in `cBld_Ring`, and five of the eight heroes are mission rewards.
Question 3 came back as "Mask of the Brave is dead in the shipped game" and that
answer is **withdrawn the same day** — it was scoped to code paths and the
starting world ships as save data (task 1). The lead the task recommended
starting from (xref `Item_CreateById`) was the one dead end; both that and the
data-channel miss are [`re-methodology.md`](docs/reference/re-methodology.md) §15.

<details>
<summary>original task text</summary>

Written to be picked up cold. Everything needed to start is below; the
background is in [`docs/subsystems/heroes.md`](docs/subsystems/heroes.md) and
[`docs/subsystems/magic-items.md`](docs/subsystems/magic-items.md), both done
2026-08-08.

**Ghidra: this needs `theocracy.real`** (game base `0x08048000`). It was the
open program on 2026-08-08, but the MCP shows one program at a time and the user
switches it manually — **ask before starting**.

#### The three questions

1. **Where do the other eight heroes enter?** `hero.cfg` places only 11 of the
   19. Ids **3, 5, 8, 9, 10, 11, 17, 19** are never placed, yet all have stat
   blocks and all but Umochi (9) have abilities and descriptions.
2. **Do Ring Piece 1 and 2 combine?** Items **24** and **25** are inert — their
   only vtable overrides forward to the default. The fully-implemented **Ring of
   Concordance (26)** is placed by no data file. `cMission_TwoRings` exists.
3. **Is Mask of the Brave (item 1) reachable at all?** No effect, no config
   constants, no description, while masks 2–7 all work. If no mission places it,
   it is **dead in the shipped game**, which is a stronger claim than what
   `magic-items.md` currently says ("inert"). Either answer is publishable; the
   current text is deliberately the weaker one.

#### Leads, already found — do not re-derive

- **20 mission classes.** `strings -a data/cd/linux/theocracy.real | grep -oE
  'cMission_[A-Za-z0-9_]+' | sort -u` → `Dragon, HeavyArmory, Josda, Josda_Pre,
  MountainVillage, S1_0, S2_0, S4_0, S5_0, S5_1, S6_0, S6_1, S8_0, Scroll,
  Scroll_lost, TheWall, TwoRings, Vampire, VillageOfJaguar, WallChecker`.
- **`cMission_TwoRings` RTTI string at `0x083a8d04`** — question 2's lead, by
  name alone. Xref it to the vtable, the vtable to the class.
- **`cMission_S4_0::Start` assert at `0x083b79a0`** — `"Hero not found. (A tili
  megint elbaszott valamit)"`. Question 1's lead, and proof that missions do
  look heroes up.
- **`Item_CreateById` at `0x0820d1f0`** — the only way an item comes into
  existence. **Xref it**: callers other than `MitemCfg_PlaceItems` (`0x08214e30`)
  and `HeroCfg_PlaceHeroes` (`0x08215200`) are exactly the mission placements,
  and that xref alone may answer questions 2 and 3 outright. Do this first — it
  is the cheapest path to a result.
- `cHero_SetHeroId` is `0x080b23d0`; hero id is a byte at man `+0x27c`.
- Functions renamed and commented in the Ghidra DB on 2026-08-08:
  `cHero_SetHeroId`, `cHero_GetName`, `Item_CreateById`, `HeroCfg_Parse`,
  `HeroCfg_PlaceHeroes`, `MitemCfg_Parse`, `MitemCfg_PlaceItems`,
  `cMagicItem_Equip_default`, `cMagicItem_Unequip_default`.

#### Tooling notes that cost time the first time

- **Decrypt before grepping any file under `data/game/`.** `selap.txt` and most
  `.cfg`/`.txt` ship `RSA4096`-XORed; grepping the ciphertext returns a
  confident false negative. [`re-methodology.md`](docs/reference/re-methodology.md)
  §13. `hero.cfg` and `mitem.cfg` are plaintext; the locale `.sdb` uses a third
  scheme (flat XOR `0x2a`) — format and reader in
  [`phls-format.md`](docs/reference/phls-format.md).
- **Compare vtable bodies, not addresses.** Every one of the 50 item vtables
  differs from the default and seven overrides are no-ops that just call it.
  §14 of the same doc.
- A full disassembly is the workhorse for xref and config-key work — regenerate
  it rather than hunting in Ghidra:

  ```sh
  objdump -d data/cd/linux/theocracy.real > /tmp/full.asm    # ~790k lines
  ```

  `LoadConfigVar` is `0x080b3de0`, called as `(globalPtr, "KEY")` with the
  global pushed **last**; scanning those pairs is how every balance constant in
  both docs got named. `tools/elfq.py game xref-call <addr>` does direct call
  xrefs without Ghidra.

#### Where findings go

Into the "Open threads" sections of the two existing docs, and their
`docs/README.md` status rows. Start a new `docs/subsystems/missions.md` only if
missions turn out to be a subsystem in their own right rather than a handful of
scripted placements — that call is yours to make once the shape is clear.

</details>

*(It was a subsystem. `docs/subsystems/missions.md` exists.)*

### 5. Smaller leftovers, worth doing only alongside something else

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
