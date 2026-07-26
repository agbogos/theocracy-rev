# Theocracy RE — docs

Reverse-engineering notes for Theocracy (Philos Laboratories, 2000) — the Linux binaries: `libmvos.so` engine + game executable + device plugins. **Project goal: run Theocracy natively on modern macOS (Apple Silicon).**

**Current architecture — [porting/guest-libmvos.md](porting/guest-libmvos.md) (playable):** map **both** `theocracy.real` and the real `libmvos.so` under Unicorn and HLE-only the finite OS/library boundary (libc / pthread / dl / sockets / SMPEG). Single-player is playable (menu → realm → units, war, save/load, cutscenes with audio). The earlier *pure-HLE native-replace* plan ([porting/macos-hle-emulator.md](porting/macos-hle-emulator.md), M1/M2) is **superseded** — it hit an unbounded GUI-reimplementation wall; kept as historical record + a source of RE'd layouts. Remaining work: `../task_fifo.md`.

## Index
- [overview.md](overview.md) — binary facts, distribution inventory, heritage note, full class/subsystem map
- [open_questions.md](open_questions.md) — remaining threads / TODO backlog
- **Porting**
  - [porting/guest-libmvos.md](porting/guest-libmvos.md) — **★ CURRENT ARCHITECTURE (playable)**: dual-image linker (`guestlink.cpp`), the OS-boundary HLE surface, render/input/audio/MPEG bring-up (milestone log G1–G11)
  - [porting/frame-timing.md](porting/frame-timing.md) — **★ crucial finding**: the present-coupled heartbeat (province 12fps → stalled clock, not slow renderer) and frame-tied simulation ("turbo" after the fix); how wall-clock-shaped bugs masquerade as performance bugs, the `THEOC_FPS` diagnostic, and the `usleep`/frame-cap fixes
  - [porting/macos-hle-emulator.md](porting/macos-hle-emulator.md) — *superseded* pure-HLE plan; still the best writeup of the game↔engine ABI contract (232 imports / 348 exports), boot sequence, subsystem contracts
  - [porting/m1-loader.md](porting/m1-loader.md) — *superseded* (single-image loader); confirmed ELF facts + trap mechanism that carried into `guestlink`
  - [porting/m2-core.md](porting/m2-core.md) — *superseded* (pure-HLE native MVOS layer); source of RE'd struct layouts (vtables, singletons, `cTextFile`, render boundary)
  - [porting/vvc_x-backend.md](porting/vvc_x-backend.md) — the X11+MIT-SHM display/input plugin, fully decompiled; the video/input contract the SDL backend traps implement
- **Reference**
  - [reference/mvos-api-inventory.md](reference/mvos-api-inventory.md) — **M0 deliverable**: full demangled API (252 classes / 2400 exports / the 232-symbol HLE boundary). Generated artifacts in `data/`; tooling in `tools/` (GNU-v2 demangler + inventory builder + `regen_api.sh`).
  - [reference/phls-format.md](reference/phls-format.md) — the `*.pck` **PHLS** archive format + `tools/phls_extract.py` (extracts the CD game data → `data/game/`); the `RSA4096` XOR text-encryption (`tools/theocracy_crypt.py`)
  - [reference/game-data-census.md](reference/game-data-census.md) — survey of the extracted data tree (7191 files): formats↔engine structs, and what feeds the sim (`selap.txt` balance, `.man`/`.dsc`/`.idx`) vs the front-end
- **Structs**
  - [structs/cGameSession.md](structs/cGameSession.md) — session struct (`g_GameSession`, full 0x58-byte layout)
  - [structs/cTribe.md](structs/cTribe.md) — faction struct (0x84; diplomacy + resources)
- **Subsystems**
  - [subsystems/memory-and-containers.md](subsystems/memory-and-containers.md) — `cSystemMemory`, `cMemBlock`, `cString`, `cList`/`cNode`
  - [subsystems/application-bootstrap.md](subsystems/application-bootstrap.md) — `cApplication` framework, **libmvos-owned `main()`**, `.so` load/global-ctors, `cVVC::OpenDisplay`, platform deps
  - [subsystems/platform-audio-threads.md](subsystems/platform-audio-threads.md) — OSS audio + software mixer, `cThread` (pthread+pipe), `cTask` (fork/execlp)
  - [subsystems/game-flow-and-main-loop.md](subsystems/game-flow-and-main-loop.md) — **game binary**: `cApplication::Init`/`Start`, main menu, `SetupGame`, `OpenRealmScreen`, movie playback
  - [subsystems/game-loop-and-simulation.md](subsystems/game-loop-and-simulation.md) — **game binary**: `RealmGameLoop` frame loop + `SimulationUpdate` fixed-timestep (lockstep-ready), province effects
  - [subsystems/simulation-step.md](subsystems/simulation-step.md) — **game binary**: `SimulationStep` one deterministic tick
  - [subsystems/multiplayer-and-factions.md](subsystems/multiplayer-and-factions.md) — 11-faction roster, MP/battle-mode flag, dev-console availability

## The binaries
- **libmvos.so** (Ghidra base `0x00010000`; file addr = Ghidra − `0x10000`) — the engine.
- **theocracy.real** (base `0x08048000`) — the game. `.symtab`-stripped, but 348 dynamic exports (incl. 64 vtables), RTTI, and rich assert strings.
- **libmvos_vvc_x.so** (+ keyboard/mouse/pointer plugins) — dlopen'd device backends, unstripped.
- **inst.linux** — unstripped installer (future CD-data extractor reference).
- Full inventory: [overview.md](overview.md#distribution-inventory-linux-folder--the-linux-cd-install-set).
- The Ghidra MCP shows **one program at a time**; switch the active program to match the doc you're working in.

## Conventions
- Addresses are Ghidra addresses of the binary the doc covers (libmvos docs: base `0x10000`; game docs: `0x08048000`).
- Findings are also written back into the Ghidra DB as decompiler comments / renames as we go.
- Naming: `c…` class, `s…` on-disk/wire struct, `t…<T>` template instantiation, `_Linux`/`_X` = platform backend.
- Decompiler gotcha: some import thunks are mis-flagged noreturn (truncated decompiles) — see note in [platform-audio-threads.md](subsystems/platform-audio-threads.md).

## Status
| Area | State |
|-----------|-------|
| Memory & containers | first pass done |
| Application bootstrap | done (main-ownership corrected); libmvos `main()` decompile pending |
| Audio / threads / processes | first pass done |
| Video/input plugin (vvc_x) | fully decompiled — contract complete |
| Game↔engine ABI contract | inventoried (232 imports / 348 exports / copy relocs) |
| Game flow / main loop | first pass done |
| In-game loop & simulation | first pass done |
| SimulationStep (one tick) | first pass done (units-mgr is next) |
| macOS port — M0 (API inventory + headers) | DONE — GNU-v2 demangler, 252-class inventory, 232-symbol boundary, `include/mvos_api.hpp`; see [reference/mvos-api-inventory.md](reference/mvos-api-inventory.md) |
| macOS port — M1/M2 pure-HLE (native-replace) | **superseded** — worked to a live render loop, then pivoted; see banners in [m1-loader.md](porting/m1-loader.md) / [m2-core.md](porting/m2-core.md) |
| **macOS port — guest-libmvos (current)** | **PLAYABLE — dual-image emulator; single-player runs (menu → realm → units, war, save/load), cutscenes with audio, 0 unimplemented traps; see [porting/guest-libmvos.md](porting/guest-libmvos.md)** |
| **macOS port — next** | Multi-hour stress harness → multiplayer; then modernisation (decouple sim from render). Full list: `../task_fifo.md`. |
| **RE findings audit** | DONE (2026-07-26) — every address `docs/` cites re-checked against the noreturn-repaired Ghidra DBs; 5 claims corrected, 1 open question closed. Findings + the provenance convention for future reads: `../task_fifo.md` (Done). |
| Everything else | mapped only (see overview) |
