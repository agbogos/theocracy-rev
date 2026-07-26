# Open Questions

The ledger of what we still **don't** know. Consolidated from the per-subsystem
docs, roughly ordered by centrality.

**IDs are stable and never reused.** They are cited from outside this file
(commit messages, `task_fifo.md`), so a resolved item is retired
to the [Closed](#closed) table rather than deleted, and a new item takes the
next free number rather than filling a gap. Numbers are therefore not in
reading order — that is deliberate.

Three states: **open** (a real unanswered question), **moot** (was a question
only because the pure-HLE approach needed it; the current architecture runs the
real engine instead), **closed** (answered — the answer lives in a subsystem
doc, and the table below says which).

---

## Open — game logic (game binary)

*The biggest gaps, and the reason the game's own internals are still mostly a
black box: everything here is simulation, which the port never had to
understand in order to run it.*

1. **Units manager** — `g_World+0x1f394` + the virtual `SimulationStep`
   dispatches; unit container `g_World+0x1f398`. Unit AI, movement, combat.
   *Largest single unknown.*
2. **`SimulationStep` sub-systems** — the per-tick manager list at
   `g_World+0x1490` (identify each registered system) and the buildings pass
   (`g_World+0x147c`).
3. **`TriggerProvinceEvent → FUN_081d6570`** — what the periodic per-province
   event actually does (unrest / miracle / random event).
4. **Entity structs / layouts:**
   - Unit / `cMan` struct (accessors `FUN_082cd2a0` / `FUN_082cc030`).
   - Province struct — fields around owner `+0x40aae` and `+0x40e84` (was
     `+0x103a1`; corrected 2026-07-26, see
     [simulation-step.md](subsystems/simulation-step.md)); record size / stride.
   - `ManIndexArray` / `BuildingIndexArray` registries (from `SetupGame`).
5. **Order / command queue** — `g_World+0x83c` (`FUN_081a2060` / `081a1fa0` /
   `081a2180`). The input / replay / MP-sync channel format.

## Open — multiplayer & determinism

6. **Confirm lockstep end-to-end** — `cMsgSender@g_World+0x5c8` send side vs.
   the IPC **receive** side consuming tick-sync messages. Partial evidence in
   hand: the netgame session seeds `RandomServer` to `0x2a` on both ends
   (`FUN_0829c300`), which is what a deterministic same-seed + command-queue +
   discrete-tick model requires — but the receive side has not been read.
8. **Faction roster template** `DAT_08645240` (11 × 0x10) — name / color / type
   / AI per faction.
9. **Realm-select UI** that writes `g_LocalFactionTable` (multiplayer only).
29. **What dispatches the netgame session** — `FUN_0829c300` is the whole
    multiplayer game start to finish, and `NetGame_AssignTeams` parses the team
    info, but **neither is called directly**: both sit in function tables
    (`0x85906a0` / `0x85907d4` / `0x84bccb4`), so entry is indirect and the
    caller is unread. Note this is now a *pure RE* question, not a blocker —
    multiplayer was verified end-to-end on 2026-07-26 (a real netgame ran
    through lobby, map selection and play), so the path evidently works; we
    simply have not read who takes it. Answering it would also close the
    remaining half of #6, since the dispatcher is where the tick-sync receive
    side is reachable from.

## Open — front-end / flow (game binary)

10. Remaining menu actions: `FUN_08145550` (tutorial / scenario launch), full
    multiplayer setup (`FUN_0829bf80`).
11. Command-line **direct-launch** path (`DAT_084c930d` / `DAT_084c9314`) —
    auto-start bypassing the menu.
12. `RollingDemoFrame` attract-mode hook — where it's driven.

## Open — engine (libmvos)

13. **`cGD` graphics device** — base vs. `_LFB8/15/16/24/32` backend dispatch
    (the vtable layout), and the bitmap / palette pipeline around it. *Partly
    answered:* the present model is documented in
    [porting/vvc_x-backend.md](porting/vvc_x-backend.md), and the **LFB16 blit
    family is fully decompiled** — `port/src/blit.cpp` carries byte-exact
    transliterations of `LFB16_PutBitmap`, `VLineAlfa`, `PutBitmap8`,
    `PutBitmap8_AMask` and `PutBitmap8C1_AMask` (**file** offsets `0x5c4e0`,
    `0x5c940`, `0x5c9b0`, `0x5cb70`, `0x5cbb0` — add `0x10000` for Ghidra),
    including the RLE packet format
    (`[count][flag]`, `flag == 0` ⇒ transparent run, else palette indices with
    index 0 as a hole) and the cdecl-despite-`__regparm` calling convention.
    Still open: the other depth backends, and how the vtable selects between
    them.
14. **`LoadDevicePlugins` internals** (`0xa4990`) — how the 4 device globals and
    the input-plugin handshake are wired. (The enclosing question — the
    Open/Close subsystem pairs — is closed; see the table.)
16. **Asset loaders** — the `c…` (runtime) ↔ `s…` (on-disk) pairings: FLC video,
    `sSPR1` / `sTER1` sprites & terrain, bitmap / font / sample / palette
    formats. *Moot for the port* (the real engine loads them all), but this is
    the single largest remaining piece of the **file-format** archaeology, and
    the one a data-modding or asset-viewer effort would need.

## Open — port / host

30. **Where the +18 KB/cycle guest-heap growth comes from** — the 20-cycle soak
    (2026-07-25) measured guest heap live rising very linearly from 11.65 to
    12.01 MB, i.e. ~7000 load/unload cycles to exhaust the 128 MB arena. Not
    chased: harmless at that rate, and G15's 50 MB/cycle class of bug is gone.
    Answering it needs an allocation-site histogram, which is the tool to build
    if it ever matters.

## Minor / deferred

18. Map loader `FUN_081c7a00` → actual file read (deferred).
19. Multiplayer scope — battle-only (tactical) vs. full strategic
    (`scenarioID = -1` + battle-map load suggests standalone tactical battles).

---

## Moot under guest-libmvos

The 2026-07-22 pivot maps the **real** `libmvos.so` under Unicorn instead of
reimplementing it, so these were questions only in service of a native
rewrite. Listed so they are not re-proposed as work — each would become live
again only if the project ever went back to native-replacing the engine.

| # | Question | Why moot |
|---|---|---|
| 21 | **`eBMType` enum** — full value table (vvc_x gives 2/4/5/6/7; 6 vs 7 unknown) | Only needed to author bitmap ctors natively; the engine's own ctors run. |
| 22 | **Game-side `cThread` subclass census** — how many guest threads and what they do | Sized a native green-thread scheduler; the guest's own threading runs under the HLE `cThread`/pipe shim. |
| 23 | **MVOS object layouts the game inlines** — systematic pass over directly-touched fields | Was the layout-compatibility surface for hand-built HLE objects. Real objects now, real layouts. (`cString`/`cNode`/`cMemBlock` were done anyway — see [memory-and-containers.md](subsystems/memory-and-containers.md).) |

Note also that #15's *port gotchas* (match the X visual depth; set
`LD_LIBRARY_PATH` for the `RTLD_LAZY` relative-name `dlopen`) are moot for the
same reason — the X11 plugin was replaced wholesale by the SDL backend, and the
plugin `dlopen`/`dlsym` handshake is synthesised by the trap layer.

---

## Closed

Retired, with where the answer now lives. Kept for the stable IDs and so a
future session can tell "answered" from "never asked".

| # | Question | Answer | Where |
|---|---|---|---|
| 7 | Netgame team-info packet consumed by `NetGame_AssignTeams` | `int32 playerCount`, then per player a `u8 playerId` followed by 7-byte records (`int16` type / `int16` amount / `int16 ?` / `u8` flag) terminated by type `0xffff`; first 5 records are the tribe's mana pools, the rest units. Includes the `playerId`→tribe mapping and why single-player always leaves the human as faction 0. | [multiplayer-and-factions.md](subsystems/multiplayer-and-factions.md) · 2026-07-26 |
| 14 | Subsystem Open/Close pairs, driven from libmvos `main()` | `main` (`0xa51e0`) → `OpenSubsystems` (`0xa4f20`) / `CloseSubsystems` (`0xa50e0`); full order documented. Sub-item `LoadDevicePlugins` internals stays open above. | [application-bootstrap.md](subsystems/application-bootstrap.md) |
| 15 | Graphics backend selection / plugin ABI / loader | `LoadDevicePlugins` (`0xa4990`) `dlopen`s `libmvos_vvc_x.so` (default device `xf86`; alt `libmvos_vvc_glide.so`) → `MapAllSymbol` `dlsym`s `Create<Type>Device` + `QueryDevice` → probe → factory → the `VVC`/`VKeyboard`/`VMouse`/`VPointer` globals. Input plugins are separate `_x.so` with the same ABI. | [porting/vvc_x-backend.md](porting/vvc_x-backend.md) |
| 17 | `priority` (`cMemBlock+0x18`) role in eviction order | There isn't one: `cSystemMemory::Alloc` evicts **oldest-first** (insert at head, walk back from the tail sentinel) and never reads `priority`. Same read found the allocator crediting each eviction's size to the budget twice — an engine bug, cold path, left alone. Still open in that doc: `cHeap_Compatibility`/`cHeapBlock` vs. `cSystemMemory`, and `cMemBlockPTR` RAII semantics. | [memory-and-containers.md](subsystems/memory-and-containers.md) · 2026-07-26 |
| 20 | Decompile libmvos `main` | Done; boot sequence documented. (Its *file* address was wrong in the docs for a while — `0x951e0`, not `0x851e0`, i.e. off by exactly the `0x10000` image base; corrected in the 2026-07-26 audit.) | [application-bootstrap.md](subsystems/application-bootstrap.md) |
| 24 | `inst.linux` `Unpack` format, for a native CD-data extractor | The `.pck` packs are gzip-wrapped **PHLS** archives (flattened DFS record block + contiguous data); `tools/phls_extract.py` extracts byte-exact — `tdat.pck` → 7191 files under `data/game/data/`. `tex.pck` is the Windows installer payload. The installer itself never had to be reversed. | [reference/phls-format.md](reference/phls-format.md) |
| 25 | libc import audit | The ~30 libc symbols (incl. glibc-internal `__strtod_internal` / `__strtol_internal`) plus pthread / dl / sockets / SMPEG are implemented in the HLE OS shim; a full boot into gameplay reports **0 unimplemented traps**. | `port/src/traps.cpp`, [porting/guest-libmvos.md](porting/guest-libmvos.md) |
| 26 | Obtain the game data (CD / ISO) | User provided the CD. Runtime reads assets from `data/cd/` (`/mnt/cdrom` remapped) and `data/game/` (extracted packs). | [porting/guest-libmvos.md](porting/guest-libmvos.md) |
| 27 | The `RSA4096` text/config encryption | Not encryption — symmetric XOR with two repeating keys (`"theocracy sux"`, period 13; `"mutant technology"`, period 17) over the body after the 7-byte `RSA4096` header. The header string is a joke. Verified byte-exact; `tools/theocracy_crypt.py`. | [reference/phls-format.md](reference/phls-format.md) |
| 28 | Where `mvos.cfg` comes from | Not in the packs — installed by `inst.linux`. We ship a hand-authored minimal `data/game/mvos.cfg`, reconstructed from the `EnvSystem` keys the boot actually reads (`[vmachine] device/fullscreen/fillobjmem/cdrom_mountpoint`, `[sound] card`, `[network] enable`). | [porting/guest-libmvos.md](porting/guest-libmvos.md) |
