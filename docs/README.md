# Theocracy RE — docs

Reverse-engineering notes for Theocracy (Philos Laboratories, 2000) — the Linux
binaries: `libmvos.so` engine + game executable + device plugins. **Project
goal: run Theocracy natively on modern machines.** It started as macOS-only and
reached macOS, Linux and Windows from one source tree; the ports are described
in [porting/other-os-ports.md](porting/other-os-ports.md). The second goal is
this doc set: the port is as much archaeology as restoration, and what was
learned about these binaries should outlive the port.

**Current architecture — [porting/guest-libmvos.md](porting/guest-libmvos.md)
(playable):** map both `theocracy.real` and the real `libmvos.so` under
Unicorn and HLE-only the finite OS/library boundary (libc / pthread / dl /
sockets / SMPEG). Single-player is playable end to end. The earlier *pure-HLE
native-replace* plan is **superseded** — it hit an unbounded
GUI-reimplementation wall; kept as historical record and as a source of RE'd
layouts.

## Why this boundary works

All RE-confirmed, and the reason the project is tractable at all:

- The game links **only `libmvos.so` and libc** — every OS dependency (X11, OSS,
  pthreads, fork, sockets, CD, dlopen) sits *behind* the libmvos boundary.
- The game imports exactly **232 symbols**: a finite, enumerable surface, versus
  the unbounded one that reimplementing the engine's GUI turned out to be.
- **libmvos owns `main()`** — the game imports it, and its own `main` is just a
  PLT thunk. The framework calls *up* into the game's `Init`/`Start`, so whoever
  provides `main` controls the entire boot. Fully decompiled in
  [subsystems/application-bootstrap.md](subsystems/application-bootstrap.md).

## Repo layout

| Path | What |
|---|---|
| `data/cd/linux/` | the game binaries — `theocracy.real`, `libmvos.so.0.9`, the `_x` device plugins, `server`, `inst.linux`. Not in git |
| `data/game/` | the extracted CD data tree (`tools/phls_extract.py` output). Not in git, except the hand-authored `mvos.cfg` |
| `data/*.tsv`, `data/mvos_api.json` | generated symbol/reloc reference tables — regenerate with `sh tools/regen_api.sh`, don't hand-edit |
| `data/commit-log.md` | the whole commit history flattened. Untracked — generate it with `python3 tools/dump_commit_log.py` |
| `docs/` | this knowledge base |
| `port/` | the emulator host (C++17 + Unicorn 2 + SDL2 + libav) — see [porting/host-architecture.md](porting/host-architecture.md) |
| `tools/` | reusable scripts: demangler, extractor, crypto, API inventory, Ghidra scripts, [`elfq.py`](../tools/elfq.py) — query either binary straight from the ELF (xrefs, PLT map, relocation-aware vtables) without Ghidra — and [`plot_health.py`](../tools/plot_health.py), which charts a `THEOC_LONGRUN` session log |
| `include/mvos_api.hpp` | generated signature reference (each method annotated with its file address) |

Build and run:

```sh
brew install unicorn sdl2 ffmpeg cmake          # one-time
cmake -S port -B port/build && cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc   # defaults to data/cd/linux/*
```

The build is platform-neutral — plain `find_path`/`find_library`, no hardcoded
prefix (`-DTHEOC_PREFIX=…` overrides the search root). For Linux there is a
container environment at [`Dockerfile`](../Dockerfile); see
[porting/other-os-ports.md](porting/other-os-ports.md).

Every runtime knob is in [porting/diagnostics.md](porting/diagnostics.md).

## Start here

| If you want to… | Read |
|---|---|
| Know who made this and what happened to them | [history.md](history.md) |
| Understand the approach and how it got here | [porting/guest-libmvos.md](porting/guest-libmvos.md) |
| Find your way around the emulator source | [porting/host-architecture.md](porting/host-architecture.md) |
| Debug something that is broken right now | [porting/diagnostics.md](porting/diagnostics.md) |
| Read the binaries without repeating our mistakes | [reference/re-methodology.md](reference/re-methodology.md) |
| Know what is still unknown | each doc's own **"Open threads"** section — start with [subsystems/simulation-step.md](subsystems/simulation-step.md) |

## The port — current

- [porting/guest-libmvos.md](porting/guest-libmvos.md) — **★ the architecture,
  told chronologically.** The dual-image linker, the OS-boundary HLE surface,
  then the milestone log G1→G21: render, input, audio, MPEG, the heap rewrite,
  cutscene skip, cursor trails, fullscreen, sockets, the dedicated server, the
  netgame lobby. The later entries are full debugging narratives — symptom,
  wrong theories, root cause.
- [porting/host-architecture.md](porting/host-architecture.md) — **the same
  system told structurally.** What each `port/src` unit owns, the guest memory
  map, the exact call paths (import trap, host→guest call, the `redirect_guest`
  green run, native overrides of real libmvos functions, in-memory guest
  patches, x87 float returns), where to add a new HLE function, and the
  invariants that a contributor would otherwise break.
- [porting/diagnostics.md](porting/diagnostics.md) — every `THEOC_*` instrument,
  what symptom each answers, and the lessons the instruments encode.
- [porting/frame-timing.md](porting/frame-timing.md) — **★ crucial finding.**
  Province view was not slow, its clock was stalled: a present-coupled
  heartbeat, a frame-tied simulation, and an fps-coupled audio mixer. The
  general lesson — under this emulator, wall-clock bugs masquerade as
  performance bugs — and how to tell them apart in one line. Also **what the
  30Hz heartbeat actually drives** (the cursor, not the simulation — a
  correction that closed the "real threads / signal delivery" item as a
  non-issue).
- [porting/heap-growth-trials.md](porting/heap-growth-trials.md) — **the leak
  hunt, by elimination — closed 2026-08-02.** Why a fifth mixed-activity long
  session would answer nothing, the one-activity-per-run protocol built on
  `Alt+M` markers, and five trials in which **every activity saturates**:
  idling, the window mode, the realm↔province sawtooth (~8 KB/cycle) and finally
  nine battles with save reloads (a plateau at ~43.6 MB, converging to 30 KB per
  reload). Nothing measured threatens the 128 MB arena; the +7–11 MB/h of the
  two long sessions was never reproduced, and the three readings that remain
  consistent with the evidence are stated rather than chosen between. Reference
  costs for reading any future log, and the nine instrument defects the trials
  exposed — five of which produced plausible wrong numbers rather than obvious
  failures.
- [porting/upscale-filtering.md](porting/upscale-filtering.md) — **done
  2026-08-02.** Why there is no true AA to be had here (no geometry, no
  higher-res art) and why the complaint is really "no CRT". Sharp-bilinear
  shipped — guest → 3× intermediate nearest → screen linear, which keeps edges
  crisp without the mush plain linear gives, fits fractionally (recovering the
  ~5% the integer floor cost) and makes scanlines free (`THEOC_SCANLINES`).
  Includes the two options deliberately rejected, and how a render change was
  geometry-tested with no display.

## The port — what's next

Open tasks are in **[`todo.md`](../todo.md)** at the repo root. It holds tasks
only; the reasoning stays in these docs. (It replaces `task_fifo.md`, retired
2026-08-03.)

**The port is a release candidate.** All three hosts are playable and verified
by play. One task is outstanding — a bare-metal Windows timing run, blocked on
hardware — and it qualifies a measurement rather than blocking anything. Beyond
it, [native-rewrite.md](porting/native-rewrite.md) is the one remaining
*direction* and what is still unknown lives in each doc's own **"Open threads"**
section. The second entry below is kept here rather than moved because it is the
record of how a host port is done in this project, not a plan.

- [porting/native-rewrite.md](porting/native-rewrite.md) — **the long game.**
  Replace the emulated engine with native C++ one function at a time, game
  playable at every step, until Unicorn has nothing left to run. Why this is not
  the superseded pure-HLE plan (we now have a running system to check each piece
  against), the seam that already exists (`blit.cpp`'s five native LFB16
  overrides), what makes a good candidate, and the hard parts — shared guest
  memory, the GUI toolkit, and knowing when to stop.
- [porting/other-os-ports.md](porting/other-os-ports.md) — **Windows and Linux
  hosts — both done.** Structured from an audit of `port/src` rather than
  porting lore. The reusable core (four of seven units already travel; all of
  `docs/` transfers); three things that look like blockers and are not
  (`fork`/`execlp` are stubs, one host thread, no host `mmap`); why **Linux is a
  subtraction** — the BSD translations are written host-macro → guest-constant,
  so on Linux they are the identity and there is nothing to neutralise (measured
  on both platforms, correcting an earlier inference that they would be
  *actively wrong*) — and the one thing that must stay; Windows' real risk
  order, led by sub-millisecond sleep against a ~15.6 ms scheduler — probed
  standalone before a line of `traps.cpp` was touched, then fixed with a
  waitable timer and re-measured in-game; the prediction that Windows would
  force a `port/src/platform/` directory into existence, and **why it was
  wrong** (three platforms build from two adjacent `#if` blocks); and **which
  binary Windows should run** — the Linux one, since the Linux binaries ship
  unprotected while the Windows build does not run as shipped on modern Windows,
  and a Windows executable would be a different compilation to which none of
  this repo's addresses apply.

## Reference & method

- [reference/re-methodology.md](reference/re-methodology.md) — how to read these
  binaries without being wrong: address-space confusion, decompiler
  pointer-arithmetic scaling, fragment addresses, Ghidra's bogus-noreturn
  analyzer, why guessed struct layouts are this port's dominant bug class, the
  evidence discipline that came out of three failed diagnoses in a row, and
  **§12 — how a plausible *name* for an unread function became a fact in four
  docs, survived the findings audit, and was caught only when a second doc read
  the same address properly**.
- [reference/mvos-api-inventory.md](reference/mvos-api-inventory.md) — **M0
  deliverable**: the GNU-v2 demangling problem and its solution, the full API
  (252 classes / 2400 exports / the 232-symbol boundary), and the honest limits
  of symbol-derived headers. Artifacts in `data/`, tooling in `tools/`.
- [reference/phls-format.md](reference/phls-format.md) — the `*.pck` **PHLS**
  archive format, byte-exact, plus the `RSA4096` XOR joke-cipher over
  config/text files. Extractor: `tools/phls_extract.py`.
- [reference/game-data-census.md](reference/game-data-census.md) — survey of the
  extracted tree (7191 files): formats ↔ engine structs, what feeds the
  simulation (`selap.txt` balance) vs. what is front-end.
- [reference/original-os-setup.md](reference/original-os-setup.md) — **running
  the shipped binaries on their intended OS**, in a VM: why Debian Woody and not
  Windows XP, the 16-bit-or-nothing colour depth, the fullscreen-and-CD launch
  ritual, and OSS audio via AC97 + `i810_audio`. The configuration in which the
  game is *entirely* original code, and therefore the reference the port is
  judged against — with each workaround tied to the finding that later explained
  it, or marked as still unexplained.
- [reference/reconf-tool.md](reference/reconf-tool.md) — **`reconf`, Philos'
  post-release reconfiguration tool — read 2026-08-15.** Not on the CD;
  recovered from a holarse.de mirror of the dead dlh.net download, and the only
  surviving Philos code that writes `mvos.cfg`. Unstripped, 78 KB, ~10 original
  functions. **Dated to 2000-09-21 by the tarball's stored mtimes — seven months
  after the CD master.** The doc keeps the case built *before* the archive
  arrived (the shipped installer says "To reconfigure please **edit** %s", this
  says "please **run reconf**"; nothing on the CD mentions it; its compiler
  package is two months newer than `inst.linux`'s), because that reasoning is
  all that will be available for the next artifact that turns up without its
  container — and because the `.comment` stamp, the piece that looked most like
  a date, was the weakest of the four. The five keys it manages (`[vmachine]`
  soundcard / cdrom_device / fullscreen / cdrom_mountpoint, `[game]` language),
  and the fstab → mtab → symlink-chase mount-point detection behind the launch
  ritual. **Then what it exposed:** our hand-authored `data/game/mvos.cfg` had
  **five of its seven lines read by no code in either binary** — the engine's
  video key is `video` not `device`, its sound key is `[vmachine] soundcard` not
  `[sound] card`, and the strings `fullscreen` and `network` occur nowhere in
  `libmvos.so` or `theocracy.real`. The two live keys were already at their
  defaults, so the file was functionally empty and worked for that reason.
  Replaced with what `inst.linux` demonstrably writes, recovered from the
  installer's own `printf` formats. `fullscreen` turns out to be **inert in the
  original game**, not just under this port. Also a free cross-check on
  `cString` (12 bytes) and `cConfigEntry` (`0x24`, is-section flag at `+0x20`),
  which it links **statically and with a `.symtab`**.
- [overview.md](overview.md) — libmvos technical report: binary facts, the CD
  distribution inventory, the **AmigaOS-heritage argument**, and the ~200-class
  map by subsystem.
- [history.md](history.md) — **Philos Laboratories, and how the game came to
  exist.** Not read off the binaries, and explicitly held to a lower standard of
  evidence than the rest of `docs/` so the two can never be confused. The
  studio; the publishing chain (Interactive Magic → **bought back by Philos** →
  Ubi Soft) and the materially different 1999 build it produced; the July 2000
  raid and the 2003 custodial sentence; why the seized drives are **not** a lost
  source cache; the sequel that was being considered. And **the one place the
  history explains the code**: `overview.md` argues AmigaOS heritage from the
  idioms alone, and the 1997 hire of the *Perihelion: The Prophecy* team — an
  Amiga title — supplies the mechanism.

## Game internals (`theocracy.real`)

Approach-independent: these describe the game itself, and would survive the port
being rewritten or abandoned.

- [subsystems/game-flow-and-main-loop.md](subsystems/game-flow-and-main-loop.md)
  — `cApplication::Init`/`Start` as a top-level state machine: single-instance
  lock, intro movies, the menu-id → action table, `SetupGame`,
  `OpenRealmScreen`.
-
  [subsystems/game-loop-and-simulation.md](subsystems/game-loop-and-simulation.md)
  — `RealmGameLoop` and `SimulationUpdate`'s fixed timestep with bounded
  catch-up; the dev-console gating.
- [subsystems/simulation-step.md](subsystems/simulation-step.md) — one
  deterministic tick; the `ALLIED_JOIN_YEARS` alliance mechanic; and the
  argument for lockstep **as weakened on 2026-08-06** — the command queue it
  rested on was the game date misread, so lockstep is now a hypothesis with the
  command channel still unfound.
- [subsystems/population-and-births.md](subsystems/population-and-births.md) —
  **how a province makes people.** `cProvince::EatHealBirth` decompiled: an
  accumulator in *person-days* (`FUCK_PER_BIRTH` = 7120 of them per child, the
  developers' own name), eligibility across all 11 tribes in the province, and
  eating/starving in the same pass. Hospitals are a **maximum, not a stack** — a
  second one contributes nothing, HOSPITAL1 does nothing for births at all, and
  HOSPITAL3 is identical to HOSPITAL2, so the whole hospital contribution is one
  flat +30% unlocked at level 2. And **the birth rate does not taper, it
  stops**: above `MAX_FUCKER` (500) the eligible count is set to *zero* rather
  than clamped, confirmed against the instruction stream at both sites. Also the
  reusable method — `selap.txt` key → `LoadConfigVar` → global → xref → code —
  which turns any balance number a player can see into the code that consumes
  it.
- [subsystems/mana-and-sacrifice.md](subsystems/mana-and-sacrifice.md) —
  **pyramids, mana and what sacrifice actually does.** `cPyramidBuilding`
  decompiled: passive mana as a carry-nothing-lost accumulator (1/day big, 1/2
  days small, via a one-line virtual per subclass), and sacrifice as a
  **42-entry queue indexed by man type** — the victims die instantly and the
  mana is paid out three per in-game day, one man at a time, at a flat rate. A
  man is worth a per-type base plus a share of his experience. And **the taper
  players see is the gauge, not the game**: `MANA_GRADIENT` drives an 11-frame
  indicator that reads full at 10,000 while the cap is 70,000, so a constant
  payout looks like a burst that fades. Includes why Ghidra's `ROUND()` is
  truncation, and the out-of-bounds bug that fact deleted.
- [subsystems/heroes.md](subsystems/heroes.md) — **what a hero actually is.**
  Not a unit type but a `cMan` carrying a hero id in one byte at `+0x27c`;
  `cHero::SetHeroId` decompiled, with a two-tier design (three generic hero *man
  types* — swordsman/spearman/archer — under nineteen named heroes). Every named
  hero is four `selap.txt` modifiers (HP/ST/ATT/DEF) plus, for some, one of
  **five magic-school immunity slots** at `+0x88..0x90` — Sun, Moon, Star,
  Nature, Soul, named by the heroes' own descriptions and agreeing across nine
  of them. Chimoki alone gets all five, at 90 rather than 100, which is
  precisely the "*partial* immunity" his description claims. Also: the
  Kathapi↔Jarakhi rivalry that exists as two symmetric config keys; `hero.cfg`
  decoded as a **placement** table (17-byte records, 8 columns in a different
  order from the fields, up to two magic items each) that places only 11 of the
  19; and three pieces of unfinished content — **Umochi, whose description is
  the string `Umochi`**, six dead `TEAMREG` keys whose names appear nowhere in
  the binary, and eight heroes never placed.
- [subsystems/magic-items.md](subsystems/magic-items.md) — **the fifty magic
  items, and the answer to "why do some have no lore".** `Item_CreateById` is a
  closed switch over ids 1–50, each item its own class; behaviour lives in five
  vtable slots (equip/unequip, tick, attack, defence) and is named by
  `selap.txt` keys that sit immediately before the spell tables. **Thirteen
  items ship with a description that is just their own name — identically in all
  six languages, so a writing gap and not a localisation one — and ten of those
  thirteen are fully implemented.** Only three are inert: the two Ring Pieces
  (which read as quest tokens for the working Ring of Concordance) and **Mask of
  the Brave**, the item equivalent of Umochi. Also the `mitem.cfg` record
  layout, the equip-restriction field, and the trap that nearly buried the whole
  finding — every one of the 50 vtables differs from the default, but seven
  overrides are no-ops that just call it.
- [subsystems/missions.md](subsystems/missions.md) — **the scripted layer, and
  the wall heroes and magic items both hit.** Twenty `cMission_*` classes over
  one base vtable (`Start`/`Activate`/`Check`/`Finish`, a three-value state
  byte), their own encrypted five-column `.man` spawn files, and the four
  province virtuals they drive. The trap first: `Item_CreateById` is *not* the
  only way an item is made — mission code calls the fifty constructors directly,
  so xref'ing the factory says "no mission places an item", which is false. Xref
  the constructors instead and the whole quest layer appears. **The Two Rings
  chain end to end**: the mission drops both halves, a building virtual nobody
  had read (`+0xd4`, "a man entered", overridden by exactly two classes)
  destroys them when one man carries both and hands him the Ring of Concordance.
  **Five of the eight unplaced heroes are mission rewards**; three more are
  assigned by no code path, and 14 items likewise. **Then the fourth channel,
  found the same day and corrected in place**: every `init.dat` is a `theosg42`
  savegame, so the starting world is *loaded*, and `cHero` has a stream
  constructor that writes the id byte straight from file data — reached through
  a caste function-pointer table, so it appears in no xref. Jarakhi is the
  campaign's player character and ships that way. "Created by nothing" is
  everywhere narrowed to "created by no code path"; the doc keeps the wrong
  version visible next to the right one.
- [subsystems/starting-world.md](subsystems/starting-world.md) — **what ships in
  `init.dat`, the campaign builder recovered, and what the Spanish cost.** Every
  map's opening state is a `theosg42` savegame, so heroes and items reach the
  game as *data* and no code audit can see them. The load chain decompiled end
  to end — `LoadGame` → `cGameSession` → `iScenario` → `cWorld` → `cProvince` →
  `cManList` → `CreateMan_fromStream`, with `LoadRegistredObject`'s class-name
  labels making the format self-describing, and the 42-entry caste table whose
  creator pointers are written at *runtime* (the indirect call that hid the hero
  constructor from every xref). Then the part that made it cheap: rather than
  re-implement ~150 stream constructors, the port **watches the game's own
  loader** (`THEOC_DUMP_WORLD`, four passive hooks, nine headless runs). The
  census of all nine worlds: **Jarakhi (11) is in the campaign file**,
  **Tlechlal (19) is in `scn6`**, **Umochi (9) is in nothing anywhere**, and
  **Mask of the Brave is in none of the nine** — so "dead in the shipped game"
  is restored, while **four of the fourteen no-code-path items do ship** and
  were placed with an editor. Also the evidence that the campaign world was
  *generated* from `hero.cfg`/`mitem.cfg` and then edited: its heroes are
  exactly `hero.cfg`'s eleven plus Jarakhi, and the only two items appearing
  twice are the only two ids both config files place. Then the **generator**:
  `SetupGame`'s three modes ("init"/"edit"/"normal") where the menu only ever
  sends "normal", the `world+0x5b4` load-vs-generate fork, and the two config
  files that ship as plain text when `cTextFile` accepts only `RSA4096` — so the
  builder could not read its own inputs. Recovered as `THEOC_NEW_WORLD` and
  **played**: half the campaign length, fewer AI provinces, a different unit
  mix, no player character and **no slaves** — a scaffold, not a campaign. And
  the Spanish: `SPAIN_ENTER_YEAR=1519` is an **absolute** date, so the start
  date *is* the campaign length — the shipped 1419 leaves 99 years, the
  generator's 1323 leaves 195.
-
  [subsystems/multiplayer-and-factions.md](subsystems/multiplayer-and-factions.md)
  — the 11-faction roster, the `+0x2c` battle-mode flag, the netgame session
  lifecycle, and the decoded team-info packet.
- [subsystems/save-format.md](subsystems/save-format.md) — **the `.tsg` save
  format and the bug that kills long campaigns.** Every save appends a
  byte-identical group to each of 44 province lists whose length is a *single
  byte* counting 17-byte units, so the counter dies at 255 — after 51 saves.
  Also the **header**: a save name, a 23–55 byte hole of uninitialised stack
  carrying live guest pointers, and the `theosg42` magic at `+0x40` that bounds
  it — so two saves of one game state never compared equal (14 of 72 bytes),
  which is what made a playtester's save hard to triage. Both are normalised at
  the same file boundary, and the window now carries a 22-byte stamp naming the
  commit, host and settings that wrote the file. Also why the save-name dialog's
  40-character cap is what keeps an unbounded `strcpy` out of reach. The fix,
  why it is anchored on the counter rather than the repetition, and why it
  refuses files it does not fully recognise.
- [subsystems/calendar.md](subsystems/calendar.md) — **the in-game calendar**,
  decompiled: the `cDate` object at `g_World+0x83c`, the seven functions that
  make it work, and the two ratios in `InitTimeUnitConstants` that fix a month
  at 20 days and a year at 365. Answers the three things hex-editing a save
  could not — the field is a plain 4-byte `int` (the old "3 bytes" was its low
  three), it has **no fixed file offset** because it follows variable-length
  data in `cWorld::Save`, and the 5 days a year that fall outside 18 months get
  **no special case at all**: they decode to month index 18 and display as a
  short 19th month.
- [subsystems/music-and-redbook.md](subsystems/music-and-redbook.md) — **the
  music: gone since the port began, working since 2026-08-08.** Theocracy's
  score is Redbook CD audio — the analogue tracks on the disc — so there is no
  music file anywhere in the data tree. The `cVCDThread` manager decompiled:
  four moods, the **track table** (menu=3, realm=8, battle={6,7} or {2,5}), the
  deterministic "random" chooser, and the 5-second polling thread that is the
  only thing that starts the next track. Why the failure was silent for so long
  (the driver ran fine; the host's blanket `ioctl` stub told it *success*), and
  the fix: a virtual CD drive in the `ioctl` trap plus a streaming decoder
  summed into the host mixer, with `cVCDThread`/`cVCD`/`cCD_Linux` all still
  running as original guest code.
- [subsystems/dev-console.md](subsystems/dev-console.md) — the developer
  console: never compiled out, gated behind one never-taken branch in
  single-player, and **half-wired even in multiplayer** (the command console is
  never given a `cShell`, so a typed command is dropped). The Alt+key dispatcher
  and eKey matrix, all four writers of the battle-mode flag, why the realm
  screen has no opener at any address, and how `THEOC_CONSOLE=1` opens it
  without patching the game.
- [structs/cGameSession.md](structs/cGameSession.md) — the session struct, full
  `0x58` layout with per-field evidence.
- [structs/cTribe.md](structs/cTribe.md) — the faction struct (`0x84`):
  diplomacy relation codes, resources, the roster template.

## Engine internals (`libmvos.so`)

- [subsystems/memory-and-containers.md](subsystems/memory-and-containers.md) —
  `cSystemMemory` as a 32 MB budgeted, evictable **asset cache**; `cMemBlock`;
  `cString` *is* a memory block; Exec-style `cList`.
- [subsystems/application-bootstrap.md](subsystems/application-bootstrap.md) —
  the framework inversion (**libmvos owns `main()`**), the decompiled 10-step
  boot, `OpenSubsystems` order, the requirement flags, and the
  platform-dependency table.
- [subsystems/platform-audio-threads.md](subsystems/platform-audio-threads.md) —
  OSS audio + software mixer, `cThread` (pthread+pipe), `cTask` (fork/execlp).
- [porting/vvc_x-backend.md](porting/vvc_x-backend.md) — the X11 + MIT-SHM
  display/input plugin, fully decompiled. The backend seam, the input entry
  points, the depth table, and the plugin `dlopen` handshake — i.e. the contract
  the SDL traps implement. Also explains why the original needed a 16-bit X
  server.

## Historical — the superseded pure-HLE approach

Each carries a banner at the top. Kept because the RE facts in them are still
accurate and still cited; the *approach* is not current.

- [porting/macos-hle-emulator.md](porting/macos-hle-emulator.md) — the original
  plan. Still the best single writeup of the game↔engine **ABI contract** (232
  imports / 348 exports / copy relocs) and the risk list.
- [porting/m1-loader.md](porting/m1-loader.md) — the single-image loader.
  Confirmed ELF facts, relocation counts, the trap mechanism, the nine flag
  addresses — all of which carried into `guestlink`.
- [porting/m2-core.md](porting/m2-core.md) — the native MVOS layer. Source of
  RE'd struct layouts (vtables, singletons, `cTextFile`, the render boundary),
  the from-scratch `sscanf`, and the x87 float-return trick that is still in
  use.

## The binaries

- **libmvos.so** (Ghidra base `0x00010000`; file addr = Ghidra − `0x10000`) —
  the engine.
- **theocracy.real** (base `0x08048000`) — the game. `.symtab`-stripped, but 348
  dynamic exports (incl. 64 vtables), RTTI, and rich assert strings.
- **libmvos_vvc_x.so** (+ keyboard/mouse/pointer plugins) — dlopen'd device
  backends, unstripped.
- **server** (47 KB) — the shipped dedicated server; boots under the same host
  (`THEOC_SERVER=1`), which is why the netgame wire protocol never had to be
  reversed.
- **inst.linux** — unstripped installer. No longer needed: the pack format was
  cracked directly (see phls-format).
- **reconf** — **not a CD binary.** Philos' post-release reconfiguration tool,
  recovered 2026-08-15 from a holarse.de mirror; unstripped, and the only
  surviving code that writes `mvos.cfg`. See
  [reference/reconf-tool.md](reference/reconf-tool.md).
- Full inventory:
  [overview.md](overview.md#distribution-inventory-linux-folder--the-linux-cd-install-set).

## Conventions

- Addresses are Ghidra addresses of the binary the doc covers (libmvos docs:
  base `0x10000`; game docs: `0x08048000`). Host log lines and
  `include/mvos_api.hpp` use libmvos **file** offsets instead — always say
  which. See [reference/re-methodology.md](reference/re-methodology.md) §1.
- The Ghidra MCP shows **one program at a time**; ask the user to switch the
  active program to match the doc you are working in.
- Findings are written back into the Ghidra DB as decompiler comments / renames
  as we go, and mirrored here. Don't leave a durable result only in a commit
  message.
- Naming: `c…` class, `s…` on-disk/wire struct, `t…<T>` template instantiation,
  `_Linux`/`_X` = platform backend.
- Symbols in the Ghidra DBs are still mangled (GNU v2 — Ghidra can't demangle
  it): use `tools/gnuv2_demangle.py` or `data/mvos_exports.tsv`.
- A lot of this project's findings were first written as commit messages.
  `python3 tools/dump_commit_log.py` flattens the whole history into
  `data/commit-log.md` so it can be read as one narrative and audited for
  anything that never reached a doc. The output is untracked — regenerate it,
  don't commit it.

## Status

| Area | State |
|-----------|-------|
| Memory & containers | first pass done |
| Application bootstrap | done — `main()`-ownership corrected and libmvos `main()` decompiled (the 10-step boot sequence is in [application-bootstrap.md](subsystems/application-bootstrap.md)) |
| Audio / threads / processes | first pass done |
| Music (CD audio / Redbook) | **both binaries read 2026-08-08** — the score is Redbook CD audio, so the port has never had music and never logged its absence. Game side: `cVCDThread`'s four moods, the track table (tracks 2–8 on the disc, **track 4 unreferenced**), the fixed-seed chooser, the poll thread. Engine side: `cVCD` is an abstract shell over `cCD_Linux`, seven plain Linux CD ioctls, stateless per call. **Implemented the same day**: `port/src/cdaudio.cpp` is a virtual drive answering those ioctls plus a streaming decoder, so the whole chain — `cVCDThread`/`cVCD`/`cCD_Linux` — runs as original guest code with nothing patched. UK disc ripped and the TOC matched the prediction made from the binary. **Played and confirmed working 2026-08-08** — music plays and switches on mood changes. The session also pinned an original-game gap: SFX and ambience sliders both work, CD music has an on/off toggle and **no volume control at all**, now the named first candidate in [native-rewrite.md](porting/native-rewrite.md) — [music-and-redbook.md](subsystems/music-and-redbook.md) |
| Video/input plugin (vvc_x) | fully decompiled — contract complete |
| Game↔engine ABI contract | inventoried (232 imports / 348 exports / copy relocs) |
| Game flow / main loop | first pass done |
| In-game loop & simulation | first pass done |
| In-game calendar | **done 2026-08-05** — read off `theocracy.real`, not inferred: the `cDate` class, the 20-day/365-day constants traced to the instructions that build them, the 4-byte save field, and the 19th month the arithmetic produces and the UI prints ([subsystems/calendar.md](subsystems/calendar.md)). Findings written back to the Ghidra DB as renames and comments. **How a tick becomes a day was answered 2026-08-06: one simulation tick is exactly one in-game day**, advanced by `cDate_Add(date, cDate(0,0,1))` after each `SimulationStep` — which also corrected a command queue that three docs cited and that does not exist |
| SimulationStep (one tick) | first pass done. **Its step 2 was corrected 2026-08-10**: `g_World+0x1f394` is not "the units manager" — there is no such object — but the `iMissionHandler`, so one tick also advances the campaign script by one day ([missions.md](subsystems/missions.md)). The unit AI/movement core is still the biggest remaining piece, but the way in is the units *container* at `+0x1f398` ([simulation-step.md](subsystems/simulation-step.md), "Open threads") |
| Mana, pyramids & sacrifice | **done 2026-08-08** — `cPyramidBuilding` (RTTI-confirmed) read off the binary, renamed and commented in the Ghidra DB. Passive mana is an accumulator with the remainder carried; sacrifice kills instantly and enqueues a per-man-type queue that pays out three per in-game day at a flat rate; a man is worth a per-type base plus experience. `MANA_GRADIENT` turned out to be **UI only** — the indicator saturates at 10,000 of a 70,000 cap, which is the "shoots up then tapers" players see. Also established that Ghidra's `ROUND()` on x87 casts is **truncation**, which killed an out-of-bounds finding before it was committed ([re-methodology.md](reference/re-methodology.md) §5) — [mana-and-sacrifice.md](subsystems/mana-and-sacrifice.md) |
| Missions | **done 2026-08-09** — the last open question from the heroes and magic-items work. Twenty `cMission_*` classes identified by their type-info getter's 20 callers, not by string-grepping; the shared vtable diffed slot by slot (`+0x14` Start, `+0x18` Check, `+0x1c` Activate, `+0x2c` Finish) and each slot confirmed against a decompiled body. The `.man` spawn format decoded (five columns, 14-byte record, `RSA4096`-encrypted) and all six files mapped to their missions. **The Two Rings combines**, via a building virtual `+0xd4` overridden by exactly two classes in the image. **Five heroes and 25 items are mission-placed**; 3 heroes and 14 items are assigned by **no code path** — which is *not* the same as absent, as a same-day correction establishes: **every `init.dat` is a `theosg42` savegame**, so the starting world is loaded rather than placed, and `cHero`'s stream constructor writes the id byte from file data. Jarakhi is the campaign's player character and arrives that way. "Mask of the Brave is dead in the shipped game" was accordingly withdrawn pending a parse of `init.dat` — and **restored the same day** once all nine world files were read ([starting-world.md](subsystems/starting-world.md)). Cost two mistakes of the same family, both in [re-methodology.md](reference/re-methodology.md) §15: **a factory tells you who uses the factory, not who builds the type** (`Item_CreateById`'s eight callers contain no mission), and then **a code audit cannot see content that ships as data** — the sweep was exhaustive over call sites and concluded the player character does not exist. **The layer above them was read 2026-08-10** and answers "what starts a mission": `iMissionHandler` is a static 13-slot table built in one constructor, **slot 0 NULL so the index is the mission number** — confirmed by the `misiNNN.man` filenames and the `MISSION_00N_*` keys agreeing with it — bound to provinces by a **hardcoded switch on province id**, and stepped by `SimulationStep` **once per in-game day**. So `MISSION_00N_YEARLEFT` is a *start delay*, not a deadline; Scroll_lost runs *because* Scroll failed; MountainVillage and HeavyArmory are the same capture-the-buildings mission twice; Josda_Pre is a gift that completes on the spot; WallChecker is a permanent poll on one map tile. Also the four timers (the dragon razing province 43 is one of them) and the **Spanish invasion**: two staggered repeating waves, re-rolled early when the player is close to winning — which closed `starting-world.md`'s `SPAIN_RND_YEAR` thread. **The mission-flag mask was read 2026-08-10** and turns out to be the fourth channel again: `man+0x28` is written by *nothing* except the two `cMan` constructors — zero, or four bytes straight out of the world file — so mission flags are **map-editor data**, every code-created man has `0`, and a campaign mission that looks its commander up can only ever find a man who shipped in `init.dat`. Only bits 0 and 1 are queried anywhere; man type 26 is the command unit and 33/34 the hero types, so `cMission_S4_0_Start`'s two lookups are "find my commander" and "find my hero" — [missions.md](subsystems/missions.md) |
| Magic items | **done 2026-08-08** — all 50 read off the binary: the closed `Item_CreateById` switch (`0x0820d1f0`), the 24/28-byte object, the five behaviour slots and their defaults, and every effect named from its `selap.txt` key. The flavour-text question is answered: **13 items have a description equal to their own name in all six languages, and 10 of the 13 work fine** — a writing gap, not cut content. The three genuinely inert ones are Ring Piece 1/2 (quest tokens for the working Ring of Concordance, unplaced by either data file) and **Mask of the Brave**, which has no constants, no effect and no text while masks 2–7 all work. `mitem.cfg` decoded (13-byte records). Cost one wrong intermediate conclusion, now [re-methodology.md](reference/re-methodology.md) §14: every one of the 50 vtables differs from the default, but seven overrides only call it. **Both of its open questions were closed on 2026-08-09** by [missions.md](subsystems/missions.md): the Ring Pieces do combine, and Mask of the Brave is dead in the shipped game — one of fourteen items nothing creates. That second answer was withdrawn and then **re-established the same day** against all nine world files, which also showed **four of the fourteen do ship** ([starting-world.md](subsystems/starting-world.md)). **Its last two field questions closed 2026-08-10**: `+0x04` is a **bitmask** AND-ed against a per-man-class capability mask for the carry test *and* an equality key for the "another item of this type" test — with types `0x80` and `0x20` exempt from the duplicate rule, so two rings are legal and two shields are not. And of the three 28-byte items, **only id 2 initialises its `+0x18`**, so Moon Shield's every-other-blow bit and Bone Horn's counter start as heap garbage. **The carrier side was read 2026-08-10**: man vtable `+0x24` is `GetItemCarryMask` and every override is a single `return <constant>`, so the whole table is sixteen numbers — **the default is `0`, so 27 of the 43 man classes can carry nothing at all**, no man carries two weapon families, and the archer is the only class whose hero variant differs (it alone gains the shield bit) — [magic-items.md](subsystems/magic-items.md) |
| Heroes | **done 2026-08-08** — `cHero::SetHeroId` (`0x080b23d0`) and `cHero::GetName` (`0x080b2b00`) read off the binary. A hero is a `cMan` with an id byte at `+0x27c`, in two tiers: three generic hero man types (33/34/35 = swordsman/spearman/archer, read from the `cLocaleEntry` constructors rather than inferred from key order) and 19 named heroes. Each named hero is four `selap.txt` modifiers plus optional abilities; the five slots at `+0x88..0x90` are per-school magic resistance, mapped to Sun/Moon/Stars/Nature/Soul by nine mutually-consistent descriptions — at the time the one claim here resting on text rather than code, and flagged as such; confirmed from the consuming code on 2026-08-10, see below. `hero.cfg` is a **placement** table, not the roster, and its consumer hands each hero up to two magic items from the factory at `0x0820d1f0` (**exactly 50 items** — the entry point for the magic-items task). Unfinished content: Umochi is a placeholder, six `TEAMREG` keys in `selap.txt` appear nowhere in the binary, eight heroes are never placed. Cracked the `.sdb` locale format on the way ([phls-format.md](reference/phls-format.md)) and added [re-methodology.md](reference/re-methodology.md) §13 after a grep of encrypted ciphertext produced a confident false negative. **The eight unplaced heroes were resolved 2026-08-09**: five are mission rewards, and of the remaining three, **Jarakhi ships in `campaign/init.dat`** and **Tlechlal in `scn6/init.dat`** — leaving **Umochi alone as placed by nothing anywhere** ([starting-world.md](subsystems/starting-world.md)). **The `+0x27c` question was closed 2026-08-10**, and the way it was posed — *the* hero id, or a general subtype byte? — was a false choice: `sizeof(cMan)` is exactly `0x27c`, so it is the first byte of the **derived** class and `cHero` and `cMan_Comm1` each declare their own field there. Established by scanning `.text` for the displacement instead of reading decompiles, which is exhaustive where a decompile sweep is not (it also catches the address-takes that made the original "sole writer" claim wrong) — now [re-methodology.md](reference/re-methodology.md) §17. The same scan found `HERO12_RANGE_MOD`'s consumer and with it a structural point: **hero abilities live in two places**, baked into the object by `SetHeroId` or recomputed live in a per-class getter. **The magic-school slots were read from the consumer side the same day**, closing this doc's last text-based claim: `+0x88` is one **five-element `u16` array**, not five fields, and the value is a **percentage damage reduction** (`dmg × (100 − resist) / 100`) rather than a flag — so Chimoki's `HERO6_MAGICRESISTANCE=90` literally means he takes a tenth of all magic damage. Four of the five schools are named by the spell classes that read their own slot (`cSpell_Sun5`/`Stars5`/`Nature4`/`Soul6`), Moon by elimination over the closed array; the third school is **Stars**, not "Star" — [heroes.md](subsystems/heroes.md) |
| Province population & births | **done 2026-08-08** — `cProvince::EatHealBirth` (`0x081db7e0`) and `cProvince::UpdateBirthRate` (`0x081db530`) read off the binary, renamed and commented in the Ghidra DB. Births are an accumulator in person-days; hospitals are a max rather than a stack, with **two tiers that buy nothing** (HOSPITAL1 for births, HOSPITAL3 entirely); and the birth rate **hits zero above 500 eligible people** instead of tapering — an explicit `= 0` where the line below it is the ordinary clamp. Offsets cross-check between the two functions, one printing scaled `int *` indices and the other byte offsets ([re-methodology.md](reference/re-methodology.md) §2). Found without Ghidra until the last step, via the `selap.txt` → `LoadConfigVar` → global → xref chain — [population-and-births.md](subsystems/population-and-births.md) |
| macOS port — M0 (API inventory + headers) | DONE — GNU-v2 demangler, 252-class inventory, 232-symbol boundary, `include/mvos_api.hpp` |
| macOS port — M1/M2 pure-HLE (native-replace) | **superseded** — worked to a live render loop, then pivoted |
| **macOS port — guest-libmvos (current)** | **PLAYABLE, single-player and multiplayer** — dual-image emulator; single-player runs end to end (menu → realm → units, war, save/load) with cutscenes and audio, 0 unimplemented traps. Multiplayer verified end-to-end 2026-07-26: the shipped dedicated server runs under the same emulator, so both ends stay original code and the wire protocol never had to be reversed |
| **Linux port** | **DONE 2026-08-03 — playable, confirmed by play.** Two BSD-isms were the entire compile delta (`sin_len`, `SO_NOSIGPIPE`). Boot, province, sockets and a 20-cycle soak verified headless in a container — the soak is *bit-identical* to macOS, live heap and frontier, all 20 cycles. Then played on real hardware: interactive input, save/load, and a full netgame (server + two clients). `tools/package-linux.sh` builds a relocatable bundle. See [porting/other-os-ports.md](porting/other-os-ports.md) |
| **Windows port** | **DONE 2026-08-04 — playable, verified by play**: a full session, save/load, a netgame and cutscene playback. Three hosts run the same i386 binaries from the same source, with no `#ifdef` outside `traps.cpp`. Two things mattered beyond the build, and both are written up in [porting/other-os-ports.md](porting/other-os-ports.md): **sub-millisecond sleep** (the risk ranked first, and the only one that was real — a waitable timer fixed it, and contention runs to 2× oversubscription closed the question), and **two implicit initialisations the port was relying on without having chosen to** — POSIX's auto-binding `listen()`, and a `WSAStartup` that a third-party DLL happened to make. Both were latent on every platform. One item outstanding: a bare-metal timing run (~2026-08-18), which qualifies a measurement rather than blocking anything. |
| **Windows port — build** | **BUILDS 2026-08-03.** `tools/package-windows.sh` cross-builds from macOS with mingw-w64 — no Windows machine is involved — producing a 7.3 MB bundle of seven DLLs taken from the real import closure, timing probe included. `traps.cpp` needed 11 fixes; the load-bearing ones were that Winsock SOCKETs are a **separate namespace from CRT fds** (libmvos polls sockets through plain `read`/`write`), `O_BINARY` everywhere (CRLF translation would silently corrupt saves and packs), and a `WSAGetLastError`→Linux-errno table. Cross-compiling also found a **real latent bug in the working macOS build** — `video.cpp` used `std::floor` without `<cmath>`. It runs the *Linux* binary. See [porting/other-os-ports.md](porting/other-os-ports.md) |
| **macOS port — next** | **Nothing outstanding.** Playability closed 2026-08-02; the modernisation list closed 2026-08-03, two of its last three items as *won't-do* once their premises were checked. Province stays at its designed 12fps and [porting/frame-timing.md](porting/frame-timing.md) says why with evidence; `THEOC_PROVINCE_MS` is the one pacing control the engine admits. Next direction: [porting/native-rewrite.md](porting/native-rewrite.md). |
| **Release engineering** | **CI complete 2026-08-21.** Every run prints its `git describe` identity as its first log line, and every packaging script names the bundle from the same string, so a bundle and its banner cannot disagree. `.github/workflows/release.yml` builds all four bundles — Linux amd64/arm64, macOS arm64, Windows x64 — on a `v*` tag push and drafts a GitHub release from them; `workflow_dispatch` runs the builds and stops. Tag-only because `origin` is Gitea with Actions disabled and push-mirrors here. Each job checks the *artefact*, not the build log: `tools/smoke-test.sh` runs the packaged binary under `THEOC_FIX_SAVE` and asserts the save-header stamp it writes against git, with wine as the runner for the cross-built `.exe`. macOS is signed with a Developer ID, notarised and deliberately unstapled (it is a `.tar.gz`, not an `.app`, and `stapler` will not write a ticket into a plain directory). Bundles are `dist/theoc-{linux-amd64,linux-arm64,macos-arm64,windows-x64}-<version>/` at 37 / 37 / 21 / 7.4 MB. See [porting/diagnostics.md](porting/diagnostics.md), "The first line names the build", and [porting/other-os-ports.md](porting/other-os-ports.md), "CI: building the bundles on GitHub". |
| Starting world (`init.dat`) | **done 2026-08-09** — the fourth channel, closed the day it was found. All nine world files read **by the game's own loader** rather than by a re-implemented parser: four passive Unicorn watches (`THEOC_DUMP_WORLD`) and nine headless runs, against a load chain ~150 stream constructors deep. **Jarakhi (11) ships in `data/campaign/init.dat`** — confirmed, not inferred; **Tlechlal (19) ships in `scn6/init.dat`**, which is the last unexplained hero answered; **Umochi (9) is in nothing at all**, the only hero of whom that is true; and **Mask of the Brave (1) is in none of the nine**, so `magic-items.md`'s withdrawn "dead in the shipped game" is **restored**. Of the fourteen no-code-path items, **four (9, 32, 44, 47) do ship** in the campaign world and ten are genuinely in nothing. The campaign world reads as *generated* from the config placers and then edited — its heroes are exactly `hero.cfg`'s eleven plus Jarakhi, and items 16 and 36 appear twice because they are the only two ids in both config files. Method written up as [re-methodology.md](reference/re-methodology.md) §16. **Then the campaign builder, recovered the same day.** `SetupGame` takes a three-way mode — "init"/"edit"/"normal" — and the menu only ever sends "normal"; "init" runs the non-stream `cWorld` ctor, leaving `world+0x5b4` at zero, which is the entire load-vs-generate fork. It also could not have worked as shipped: `hero.cfg` and `mitem.cfg` are two of only four files in the tree with no `RSA4096` magic, and `cTextFile` accepts nothing else. `THEOC_NEW_WORLD` selects the mode and serves converted copies in memory, leaving the tree as-shipped; `save` is redirected off `init.dat` so it cannot destroy the shipped campaign. **Generated and played**: date 1323/07/04 against the shipped 1419/07/04, fewer AI provinces, a different unit mix, no Jarakhi and **no slaves** — a scaffold a designer was meant to finish, which is what the shipped world is. Also **the Spanish**: `SPAIN_ENTER_YEAR=1519` is absolute, so the start date is the campaign length — 99 years shipped against 195 generated — while the mission deadlines are relative and were never retuned — [starting-world.md](subsystems/starting-world.md) |
| RE findings audit | DONE (2026-07-26) — every address `docs/` cites re-checked against the noreturn-repaired Ghidra DBs; 5 claims corrected, 1 open question closed. Method distilled into [reference/re-methodology.md](reference/re-methodology.md) |
| `reconf` (post-release tool) | **done 2026-08-15** — a new artifact, not a CD binary: found on a holarse.de mirror of the dead dlh.net download and read end to end the same day. **Dated 2000-09-21 10:11:37 UTC** by the tarball's tar and gzip timestamps, against a CD mastered 23–25 Feb 2000 — so post-release by seven months, and by measurement rather than inference. The case built *before* the archive arrived is kept in the doc and all of it agrees: `inst.linux` tells the user to *edit* the config by hand while `reconf` tells them to *run reconf*, nothing anywhere on the CD mentions it, and its compiler package (gcc 2.95.2 20000116) is two months newer than the installer's (19991109) while the game and engine are older egcs-2.91.60. The lesson kept with it is that the `.comment` stamp — the piece that most looked like a date — was the weakest, giving only a lower bound that fell eight months short. Decompiled: `main`, `CreateConfig` (all 2444 lines of it), `FindMountPoint`, `SearchInSysTab`, `GetAnswer`, the static init. It edits `~/.theocracy/mvos.cfg`, falls back to `/usr/games/theocracy_base/mvos.cfg`, and **aborts rather than writing a file from nothing**. Section names had to be read off the instruction stream at `0x0804af3f`/`0x0804b455` because Ghidra drops the string arguments to the inlined `cString` constructors. **Then it paid for itself**: chasing its five keys into `libmvos.so` showed that **five of the seven lines in our hand-authored `data/game/mvos.cfg` are read by nothing** (`device` should be `video`; `[sound] card` should be `[vmachine] soundcard`; `fullscreen` and `network` appear in neither binary), while the two live keys sat at their defaults — so the config we shipped for a year was functionally empty, and worked for exactly that reason. The engine's real vocabulary is five keys, each with a hardcoded fallback, now tabulated in [application-bootstrap.md](subsystems/application-bootstrap.md), whose own "config vocabulary" line was wrong in the same two places. `data/game/mvos.cfg` was replaced with what `inst.linux` provably writes — recovered from the installer's `printf` format strings, every value equal to the engine's default, so the change is behaviour-neutral by construction. `fullscreen` is **inert in the shipped game**, not merely under this port — [reconf-tool.md](reference/reconf-tool.md) |
| Everything else | mapped only (see [overview.md](overview.md)) |

## Legal note

Reverse-engineering a copy of the game you own, for personal use, is fine.
*Distributing* a reconstructed port sits in the same grey zone devilutionX lives
in — Philos Laboratories is defunct and the rights are in limbo.

**"In limbo" is more specific than it used to be here.** This file previously
said the rights sit "via Ubisoft" in limbo, which assumed the answer. The
publishing chain ([history.md](history.md)) does not support that assumption:
Interactive Magic held the publishing rights during development, dropped boxed
releases in March 1999, **Philos bought those rights back**, and then handed
them to Ubi Soft. A studio that can buy publication rights back from one
publisher and grant them to another is behaving like the copyright holder
licensing publication — not like a party that has sold its IP. The phrase used
consistently in the primary accounts is *publishing rights*, never the IP.

**Settled 2026-08-15, off the retail box.** The packaging carries the copyright
in **Philos Laboratories**, with the game *"licensed exclusively to Ubi Soft"*.
That is the licence branch, stated by the rights-holder on the article itself:
**Ubi Soft was the licensee, not the owner.** Ubisoft never acquired the IP.

This is direct observation of the physical release rather than a secondary
account, which makes it better-evidenced than most of [history.md](history.md) —
and it decides a question that has apparently been open in public for
twenty-five years.

What it settles:

- **Ubisoft does not own Theocracy**, so the reason it has never reached a
  storefront under them is most likely that they *cannot* license it, not that
  nobody got round to it.
- **The copyright was Philos', and Philos dissolved in 2004.** So it now sits
  wherever a dissolved Hungarian company's residual assets went — creditors, a
  founder, or unadministered. That is the orphan-work branch.

What it does **not** settle, and this is the part worth not overreading:

- **The licence's term and scope are not printed on a box**, and "exclusively"
  is doing real work. An exclusive publishing licence can outlive the publisher's
  interest in exercising it; if it has no expiry, Ubi Soft's successor may still
  hold exclusive publication rights over a game it does not own and will not
  publish. A re-release could therefore need *two* signatures, not one.
- It does not make distributing a reconstructed port lawful, and none of this is
  legal advice.

The practical effect on this repository is small but real: the honest
description is now **orphan work, owner unlocated** rather than *unknown, maybe
Ubisoft's*, and the party this project would once have worried about turns out
not to be the rights-holder at all.

**Nothing copyrighted by the rights-holder is in this repository**, and that is
maintained deliberately rather than incidentally. Audited 2026-08-03:

- **No game binaries or assets.** `.gitignore` excludes `data/cd/` (the disc) and
  `data/game/*` (the extracted tree); both must be supplied out of band. Verified
  against the tracked file list, not just the ignore rules: 68 files, all source,
  documentation, or derived reference tables.
- **The derived tables** (`data/*.tsv`, `data/mvos_api.json`,
  `include/mvos_api.hpp`) hold **symbol names, addresses and signatures** — the
  interface facts needed for interoperability, not expressive content, and not
  usable to reconstruct the game.
- **`data/game/mvos.cfg` is 7 lines of config** normally written by the
  installer, and since 2026-08-15 it reproduces what `inst.linux` writes — five
  key-value pairs and two section headers, every value the engine's own default.
  Facts about an interface, with no expressive content to own.
- **Every tool here is our own implementation.** Nothing third-party is
  vendored.
- **The port never decrypts anything.** The engine's real `cTextFile` runs as
  guest code and decrypts on read exactly as it did in 2000; the host only serves
  bytes, and the canonical data tree stays as-shipped.

## Licence

**Our own code is `GPL-2.0-or-later`** (`LICENSE`, verbatim GPL-2.0 text; every
file we wrote carries an SPDX header). The choice is forced rather than
preferred: Unicorn 2.x declares `GPL-2.0-only AND GPL-2.0-or-later` — it carries
QEMU-derived v2-only files — which makes AGPLv3 binaries undistributable. "or
later" rather than v2-only keeps an AGPL move open if
[porting/native-rewrite.md](porting/native-rewrite.md) ever retires Unicorn. The
rest of the bundle is compatible: ffmpeg is LGPL-2.1+ built **without**
`--enable-gpl`/`--enable-nonfree`, SDL2 is zlib, libwinpthread is the permissive
mingw-w64 runtime.

**[`THIRD-PARTY.md`](../THIRD-PARTY.md) is the policy and the written offer for
source.** The per-build evidence is a `THIRD-PARTY.txt` generated *inside* every
bundle by [`tools/third-party.sh`](../tools/third-party.sh), because the Linux
bundle alone ships 52 libraries and a hand-kept list would be wrong within a
Debian point release. It records each file's source package and exact version —
`apt-get source <pkg>=<version>` retrieves precisely what shipped — and warns
loudly if it cannot attribute something. Note that Unicorn is a *different
version on each platform* (Debian's package, Homebrew's formula, a pinned source
build) and on Windows is **statically linked into `theoc.exe`** rather than
shipped as a DLL, so the Windows manifest names it separately.

**What deliberately carries no header**, and the omission is the point:
`include/mvos_api.hpp`, `data/*.tsv` and `data/mvos_api.json` are *generated
from the game binary* — symbol names, addresses and signatures. The audit above
calls those interface facts rather than expressive content, and stamping our
copyright on them would quietly contradict it. The **generators** are ours and
are headed; their output is not.

One thing a reader should still decide rather than infer:
`tools/theocracy_crypt.py` implements the game's own trivial config obfuscation
for reading data you already own — documented in
[reference/phls-format.md](reference/phls-format.md). That was filed as "a
judgement call worth making explicitly if this is ever distributed"; with
releases now planned it has stopped being hypothetical, and it is **open**.
