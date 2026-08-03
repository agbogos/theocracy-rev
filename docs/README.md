# Theocracy RE — docs

Reverse-engineering notes for Theocracy (Philos Laboratories, 2000) — the Linux
binaries: `libmvos.so` engine + game executable + device plugins. **Project goal:
run Theocracy natively on modern macOS (Apple Silicon).** The second goal is this
doc set: the port is as much archaeology as restoration, and what was learned
about these binaries should outlive the port.

**Current architecture — [porting/guest-libmvos.md](porting/guest-libmvos.md)
(playable):** map **both** `theocracy.real` and the real `libmvos.so` under
Unicorn and HLE-only the finite OS/library boundary (libc / pthread / dl /
sockets / SMPEG). Single-player is playable end to end. The earlier *pure-HLE
native-replace* plan is **superseded** — it hit an unbounded GUI-reimplementation
wall; kept as historical record and as a source of RE'd layouts.

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

Every runtime knob is in [porting/diagnostics.md](porting/diagnostics.md).

## Start here

| If you want to… | Read |
|---|---|
| Understand the approach and how it got here | [porting/guest-libmvos.md](porting/guest-libmvos.md) |
| Find your way around the emulator source | [porting/host-architecture.md](porting/host-architecture.md) |
| Debug something that is broken right now | [porting/diagnostics.md](porting/diagnostics.md) |
| Read the binaries without repeating our mistakes | [reference/re-methodology.md](reference/re-methodology.md) |
| Know what is still unknown | [open_questions.md](open_questions.md) |

## The port — current

- [porting/guest-libmvos.md](porting/guest-libmvos.md) — **★ the architecture, told chronologically.** The dual-image linker, the OS-boundary HLE surface, then the milestone log G1→G21: render, input, audio, MPEG, the heap rewrite, cutscene skip, cursor trails, fullscreen, sockets, the dedicated server, the netgame lobby. The later entries are full debugging narratives — symptom, wrong theories, root cause.
- [porting/host-architecture.md](porting/host-architecture.md) — **the same system told structurally.** What each `port/src` unit owns, the guest memory map, the exact call paths (import trap, host→guest call, the `redirect_guest` green run, native overrides of real libmvos functions, in-memory guest patches, x87 float returns), where to add a new HLE function, and the invariants that a contributor would otherwise break.
- [porting/diagnostics.md](porting/diagnostics.md) — every `THEOC_*` instrument, what symptom each answers, and the lessons the instruments encode.
- [porting/frame-timing.md](porting/frame-timing.md) — **★ crucial finding.** Province view was not slow, its clock was stalled: a present-coupled heartbeat, a frame-tied simulation, and an fps-coupled audio mixer. The general lesson — under this emulator, wall-clock bugs masquerade as performance bugs — and how to tell them apart in one line. Also **what the 30Hz heartbeat actually drives** (the cursor, not the simulation — a correction that closed the "real threads / signal delivery" item as a non-issue).
- [porting/heap-growth-trials.md](porting/heap-growth-trials.md) — **the leak hunt, by elimination — closed 2026-08-02.** Why a fifth mixed-activity long session would answer nothing, the one-activity-per-run protocol built on `Alt+M` markers, and five trials in which **every activity saturates**: idling, the window mode, the realm↔province sawtooth (~8 KB/cycle) and finally nine battles with save reloads (a plateau at ~43.6 MB, converging to 30 KB per reload). Nothing measured threatens the 128 MB arena; the +7–11 MB/h of the two long sessions was never reproduced, and the three readings that remain consistent with the evidence are stated rather than chosen between. Reference costs for reading any future log, and the nine instrument defects the trials exposed — five of which produced plausible wrong numbers rather than obvious failures.
- [porting/upscale-filtering.md](porting/upscale-filtering.md) — **done 2026-08-02.** Why there is no true AA to be had here (no geometry, no higher-res art) and why the complaint is really "no CRT". Sharp-bilinear shipped — guest → 3× intermediate nearest → screen linear, which keeps edges crisp without the mush plain linear gives, fits fractionally (recovering the ~5% the integer floor cost) and makes scanlines free (`THEOC_SCANLINES`). Includes the two options deliberately rejected, and how a render change was geometry-tested with no display.

## The port — what's next

The worklist (`task_fifo.md`) was **retired on 2026-08-03** when its last item
closed. There is no worklist file; these two are the directions, and
[open_questions.md](open_questions.md) is what is still unknown.

- [porting/native-rewrite.md](porting/native-rewrite.md) — **the long game.** Replace the emulated engine with native C++ one function at a time, game playable at every step, until Unicorn has nothing left to run. Why this is not the superseded pure-HLE plan (we now have a running system to check each piece against), the seam that already exists (`blit.cpp`'s five native LFB16 overrides), what makes a good candidate, and the hard parts — shared guest memory, the GUI toolkit, and knowing when to stop.
- [porting/other-os-ports.md](porting/other-os-ports.md) — **Windows and Linux hosts**, structured from an audit of `port/src` rather than porting lore. The reusable core (four of seven units already travel; all of `docs/` transfers); three things that look like blockers and are not (`fork`/`execlp` are stubs, one host thread, no host `mmap`); why **Linux is a subtraction** — the BSD translations are unconditional and would be *actively wrong* left in — and the one that must stay; Windows' real risk order, led by sub-millisecond sleep against a ~15.6 ms scheduler; and **which binary Windows should run** — the Linux one, since the Linux binaries ship unprotected while the Windows build does not run as shipped on modern Windows, and a Windows executable would be a different compilation to which none of this repo's addresses apply.

## Reference & method

- [reference/re-methodology.md](reference/re-methodology.md) — how to read these binaries without being wrong: address-space confusion, decompiler pointer-arithmetic scaling, fragment addresses, Ghidra's bogus-noreturn analyzer, why guessed struct layouts are this port's dominant bug class, and the evidence discipline that came out of three failed diagnoses in a row.
- [reference/mvos-api-inventory.md](reference/mvos-api-inventory.md) — **M0 deliverable**: the GNU-v2 demangling problem and its solution, the full API (252 classes / 2400 exports / the 232-symbol boundary), and the honest limits of symbol-derived headers. Artifacts in `data/`, tooling in `tools/`.
- [reference/phls-format.md](reference/phls-format.md) — the `*.pck` **PHLS** archive format, byte-exact, plus the `RSA4096` XOR joke-cipher over config/text files. Extractor: `tools/phls_extract.py`.
- [reference/game-data-census.md](reference/game-data-census.md) — survey of the extracted tree (7191 files): formats ↔ engine structs, what feeds the simulation (`selap.txt` balance) vs. what is front-end.
- [overview.md](overview.md) — libmvos technical report: binary facts, the CD distribution inventory, the **AmigaOS-heritage argument**, and the ~200-class map by subsystem.
- [open_questions.md](open_questions.md) — the ledger of what is still unknown: open threads, the ones the pivot made moot, and a closed table pointing at where each answer landed. IDs are stable and cited from commits — never renumber.

## Game internals (`theocracy.real`)

Approach-independent: these describe the game itself, and would survive the port
being rewritten or abandoned.

- [subsystems/game-flow-and-main-loop.md](subsystems/game-flow-and-main-loop.md) — `cApplication::Init`/`Start` as a top-level state machine: single-instance lock, intro movies, the menu-id → action table, `SetupGame`, `OpenRealmScreen`.
- [subsystems/game-loop-and-simulation.md](subsystems/game-loop-and-simulation.md) — `RealmGameLoop` and `SimulationUpdate`'s fixed timestep with bounded catch-up; the dev-console gating.
- [subsystems/simulation-step.md](subsystems/simulation-step.md) — one deterministic tick, and the argument for lockstep (command queue + shared seeded RNG + discrete ticks).
- [subsystems/multiplayer-and-factions.md](subsystems/multiplayer-and-factions.md) — the 11-faction roster, the `+0x2c` battle-mode flag, the netgame session lifecycle, and the decoded team-info packet.
- [subsystems/save-format.md](subsystems/save-format.md) — **the `.tsg` save format and the bug that kills long campaigns.** Every save appends a byte-identical group to each of 44 province lists whose length is a *single byte* counting 17-byte units, so the counter dies at 255 — after 51 saves. Also: the 72-byte header is 54 bytes of uninitialised stack, written to disk with live guest pointers in it. The fix, why it is anchored on the counter rather than the repetition, and why it refuses files it does not fully recognise.
- [subsystems/dev-console.md](subsystems/dev-console.md) — the developer console: never compiled out, gated behind one never-taken branch in single-player, and **half-wired even in multiplayer** (the command console is never given a `cShell`, so a typed command is dropped). The Alt+key dispatcher and eKey matrix, all four writers of the battle-mode flag, why the realm screen has no opener at any address, and how `THEOC_CONSOLE=1` opens it without patching the game.
- [structs/cGameSession.md](structs/cGameSession.md) — the session struct, full `0x58` layout with per-field evidence.
- [structs/cTribe.md](structs/cTribe.md) — the faction struct (`0x84`): diplomacy relation codes, resources, the roster template.

## Engine internals (`libmvos.so`)

- [subsystems/memory-and-containers.md](subsystems/memory-and-containers.md) — `cSystemMemory` as a 32 MB budgeted, evictable **asset cache**; `cMemBlock`; `cString` *is* a memory block; Exec-style `cList`.
- [subsystems/application-bootstrap.md](subsystems/application-bootstrap.md) — the framework inversion (**libmvos owns `main()`**), the decompiled 10-step boot, `OpenSubsystems` order, the requirement flags, and the platform-dependency table.
- [subsystems/platform-audio-threads.md](subsystems/platform-audio-threads.md) — OSS audio + software mixer, `cThread` (pthread+pipe), `cTask` (fork/execlp).
- [porting/vvc_x-backend.md](porting/vvc_x-backend.md) — the X11 + MIT-SHM display/input plugin, fully decompiled. The backend seam, the input entry points, the depth table, and the plugin `dlopen` handshake — i.e. the contract the SDL traps implement. Also explains why the original needed a 16-bit X server.

## Historical — the superseded pure-HLE approach

Each carries a banner at the top. Kept because the RE facts in them are still
accurate and still cited; the *approach* is not current.

- [porting/macos-hle-emulator.md](porting/macos-hle-emulator.md) — the original plan. Still the best single writeup of the game↔engine **ABI contract** (232 imports / 348 exports / copy relocs) and the risk list.
- [porting/m1-loader.md](porting/m1-loader.md) — the single-image loader. Confirmed ELF facts, relocation counts, the trap mechanism, the nine flag addresses — all of which carried into `guestlink`.
- [porting/m2-core.md](porting/m2-core.md) — the native MVOS layer. Source of RE'd struct layouts (vtables, singletons, `cTextFile`, the render boundary), the from-scratch `sscanf`, and the x87 float-return trick that is still in use.

## The binaries

- **libmvos.so** (Ghidra base `0x00010000`; file addr = Ghidra − `0x10000`) — the engine.
- **theocracy.real** (base `0x08048000`) — the game. `.symtab`-stripped, but 348 dynamic exports (incl. 64 vtables), RTTI, and rich assert strings.
- **libmvos_vvc_x.so** (+ keyboard/mouse/pointer plugins) — dlopen'd device backends, unstripped.
- **server** (47 KB) — the shipped dedicated server; boots under the same host (`THEOC_SERVER=1`), which is why the netgame wire protocol never had to be reversed.
- **inst.linux** — unstripped installer. No longer needed: the pack format was cracked directly (see phls-format).
- Full inventory: [overview.md](overview.md#distribution-inventory-linux-folder--the-linux-cd-install-set).

## Conventions

- Addresses are Ghidra addresses of the binary the doc covers (libmvos docs: base `0x10000`; game docs: `0x08048000`). Host log lines and `include/mvos_api.hpp` use libmvos **file** offsets instead — always say which. See [reference/re-methodology.md](reference/re-methodology.md) §1.
- The Ghidra MCP shows **one program at a time**; ask the user to switch the active program to match the doc you are working in.
- Findings are written back into the Ghidra DB as decompiler comments / renames as we go, and mirrored here. Don't leave a durable result only in a commit message.
- Naming: `c…` class, `s…` on-disk/wire struct, `t…<T>` template instantiation, `_Linux`/`_X` = platform backend.
- Symbols in the Ghidra DBs are still mangled (GNU v2 — Ghidra can't demangle it): use `tools/gnuv2_demangle.py` or `data/mvos_exports.tsv`.
- A lot of this project's findings were first written as commit messages. `python3 tools/dump_commit_log.py` flattens the whole history into `data/commit-log.md` so it can be read as one narrative and audited for anything that never reached a doc. The output is untracked — regenerate it, don't commit it.

## Status

| Area | State |
|-----------|-------|
| Memory & containers | first pass done |
| Application bootstrap | done — `main()`-ownership corrected **and** libmvos `main()` decompiled (the 10-step boot sequence is in [application-bootstrap.md](subsystems/application-bootstrap.md)) |
| Audio / threads / processes | first pass done |
| Video/input plugin (vvc_x) | fully decompiled — contract complete |
| Game↔engine ABI contract | inventoried (232 imports / 348 exports / copy relocs) |
| Game flow / main loop | first pass done |
| In-game loop & simulation | first pass done |
| SimulationStep (one tick) | first pass done (units manager is the next target, open question #1) |
| macOS port — M0 (API inventory + headers) | DONE — GNU-v2 demangler, 252-class inventory, 232-symbol boundary, `include/mvos_api.hpp` |
| macOS port — M1/M2 pure-HLE (native-replace) | **superseded** — worked to a live render loop, then pivoted |
| **macOS port — guest-libmvos (current)** | **PLAYABLE, single-player and multiplayer** — dual-image emulator; single-player runs end to end (menu → realm → units, war, save/load) with cutscenes and audio, 0 unimplemented traps. Multiplayer verified end-to-end 2026-07-26: the shipped dedicated server runs under the same emulator, so both ends stay original code and the wire protocol never had to be reversed |
| **macOS port — next** | **Nothing outstanding.** Playability closed 2026-08-02; the modernisation list closed 2026-08-03, the last three items in one day — two of them as *won't-do* once their premises were checked. Province stays at its designed 12fps and [porting/frame-timing.md](porting/frame-timing.md) says why with evidence; `THEOC_PROVINCE_MS` is the one pacing control the engine admits. The worklist file is retired. Next directions: [porting/native-rewrite.md](porting/native-rewrite.md) and [porting/other-os-ports.md](porting/other-os-ports.md); still-unknown: [open_questions.md](open_questions.md) |
| RE findings audit | DONE (2026-07-26) — every address `docs/` cites re-checked against the noreturn-repaired Ghidra DBs; 5 claims corrected, 1 open question closed. Method distilled into [reference/re-methodology.md](reference/re-methodology.md) |
| Everything else | mapped only (see [overview.md](overview.md)) |

## Legal note

Reverse-engineering a copy of the game you own, for personal use, is fine.
*Distributing* a reconstructed port sits in the same grey zone devilutionX lives
in — Philos Laboratories is defunct and the rights, via Ubisoft, are in limbo.

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
- **`data/game/mvos.cfg` is ours** — 11 lines, hand-authored, normally written by
  the installer.
- **Every tool here is our own implementation.** Nothing third-party is vendored.
- **The port never decrypts anything.** The engine's real `cTextFile` runs as
  guest code and decrypts on read exactly as it did in 2000; the host only serves
  bytes, and the canonical data tree stays as-shipped.

Two things a reader should decide for themselves rather than infer from this
file: the repository carries **no `LICENSE`** (so default "all rights reserved"
applies to our own code), and `tools/theocracy_crypt.py` implements the game's
own trivial config obfuscation for reading data you already own — documented in
[reference/phls-format.md](reference/phls-format.md), and a judgement call worth
making explicitly if this is ever distributed.
