# Open Questions

Consolidated list of remaining threads, deduped from the per-subsystem docs. Roughly ordered by centrality. Check items off / move to the relevant doc as they're resolved.

## Game logic (game binary) — biggest gaps
1. **Units manager** — `g_World+0x1f394` + the virtual `SimulationStep` dispatches; unit container `g_World+0x1f398`. Unit AI, movement, combat. *Largest single unknown.*
2. **`SimulationStep` sub-systems** — the per-tick manager list at `g_World+0x1490` (identify each registered system) and the buildings pass (`g_World+0x147c`).
3. **`TriggerProvinceEvent → FUN_081d6570`** — what the periodic per-province event actually does (unrest / miracle / random event).
4. **Entity structs / layouts:**
   - Unit / `cMan` struct (accessors `FUN_082cd2a0` / `FUN_082cc030`).
   - Province struct — fields around owner `+0x40aae` and `+0x103a1`; record size / stride.
   - `ManIndexArray` / `BuildingIndexArray` registries (from `SetupGame`).
5. **Order / command queue** — `g_World+0x83c` (`FUN_081a2060` / `081a1fa0` / `081a2180`). The input / replay / MP-sync channel format.

## Multiplayer & determinism
6. **Confirm lockstep end-to-end** — `cMsgSender@g_World+0x5c8` send side vs. the IPC **receive** side consuming tick-sync messages.
7. **Netgame team-info packet** consumed by `NetGame_AssignTeams` (players → faction slots, commander / mana assignment).
8. **Faction roster template** `DAT_08645240` (11 × 0x10) — name / color / type / AI per faction.
9. **Realm-select UI** that writes `g_LocalFactionTable` (multiplayer only).

## Front-end / flow (game binary)
10. Remaining menu actions: `FUN_08145550` (tutorial / scenario launch), full multiplayer setup (`FUN_0829bf80`).
11. Command-line **direct-launch** path (`DAT_084c930d` / `DAT_084c9314`) — auto-start bypassing the menu.
12. `RollingDemoFrame` attract-mode hook — where it's driven.

## Engine (libmvos) — mapped but not opened
13. **`cGD` graphics device** — base vs. `_LFB8/15/16/24/32` backend dispatch (vtable layout); the bitmap / palette / blit pipeline. *(Backend present-model now known — see [porting/vvc_x-backend.md](porting/vvc_x-backend.md); LFB blit pipeline still open.)*
14. **Subsystem Open/Close pairs** — ~~driven from libmvos `main()`~~ **RESOLVED**: `main` (`0xa51e0`) → `OpenSubsystems` (`0xa4f20`) / `CloseSubsystems` (`0xa50e0`), full order documented in [subsystems/application-bootstrap.md](subsystems/application-bootstrap.md). Remaining sub-item: `LoadDevicePlugins` (`0xa4990`) internals (how the 4 device globals + input-plugin handshake are wired).
15. ~~**Graphics backend selection / plugin ABI / loader.**~~ **RESOLVED (both sides):** `LoadDevicePlugins` (libmvos `0xa49a0`) `dlopen`s `libmvos_vvc_x.so` (default device `xf86`; alt `libmvos_vvc_glide.so`) → `MapAllSymbol` `dlsym`s `Create<Type>Device`+`QueryDevice` → probe → factory → global `VVC`/`VKeyboard`/`VMouse`/`VPointer`. Input is separate `_x.so` plugins (same ABI). See [porting/vvc_x-backend.md](porting/vvc_x-backend.md).
    - **Port gotchas:** (a) `SetVideoMode` needs the X visual depth to match (game asks 16/15-bit; modern X is 24-bit → fails → run 16-bit Xephyr or convert); (b) plugins are `dlopen`'d by relative name `RTLD_LAZY` → set `LD_LIBRARY_PATH`.
16. **Asset loaders** — the `c…` (runtime) ↔ `s…` (on-disk) pairings: FLC video, `sSPR1` / `sTER1` sprites & terrain, bitmap / font / sample / palette formats.
17. **Memory loose ends** — `priority` (`cMemBlock+0x18`) role in eviction order; `cHeap_Compatibility` / `cHeapBlock` (second allocator) vs. `cSystemMemory`; `cMemBlockPTR` RAII semantics.

## macOS port (see [porting/guest-libmvos.md](porting/guest-libmvos.md))

> **Approach pivoted (2026-07-22) to guest-libmvos** — real libmvos runs under
> Unicorn, so questions that only mattered for *reimplementing* libmvos are now
> **moot**: #13 (`cGD` LFB blit pipeline), #21 (`eBMType` enum), #22 (`cThread`
> subclass census, for a native scheduler), #23 (MVOS object layouts the game
> inlines). The libc surface (#25) is implemented (0 unimplemented traps). The
> game-logic threads (#1–12, #16–19) remain valid — they're about the game's own
> internals, independent of the port approach.

20. ~~Decompile libmvos `main`~~ **DONE** — boot sequence documented in [subsystems/application-bootstrap.md](subsystems/application-bootstrap.md); next: `LoadDevicePlugins` (`0xa4990`).
21. **`eBMType` enum** — full value table (vvc_x gives 2/4/5/6/7; 6 vs 7 distinction unknown; used everywhere in bitmap ctors).
22. **Game-side `cThread` subclass census** — how many guest threads exist and what they do (sizes the green-thread scheduler). Search game vtables for `cThread` method slots.
23. **MVOS object layouts the game inlines** — systematic pass: which imported classes' fields does game code touch directly? (Determines the layout-compatibility surface for HLE objects; cString/cNode/cMemBlock already done in [subsystems/memory-and-containers.md](subsystems/memory-and-containers.md).)
24. ~~**`inst.linux` `Unpack` format** — RE the installer's container to build a native CD-data extractor.~~ **RESOLVED** — the `.pck` packs are gzip-wrapped **PHLS** archives (flattened DFS record block + contiguous data); `tools/phls_extract.py` extracts byte-exact (`tdat.pck` → 7191 files under `data/game/data/`). Format: [reference/phls-format.md](reference/phls-format.md). Two follow-ups fell out:
    - ~~**27. `RSA4096` text/config encryption**~~ **RESOLVED** — symmetric XOR with two repeating keys (`"theocracy sux"` period 13, `"mutant technology"` period 17) over the body after the 7-byte `RSA4096` header (user supplied the recovered `XorBuff`, in `tools/crypt/TheocracyEncDec.cpp`). Verified byte-exact. Tool: `tools/theocracy_crypt.py`. M2 ports `XorBuff` into the HLE `cTextFile` read path.
    - ~~**28. `mvos.cfg` source**~~ **RESOLVED** — not in the packs; we ship a hand-authored minimal `data/game/mvos.cfg` (the libmvos cfg loader reads it), tracked via a `.gitignore` carve-out. Reconstructed from the `EnvSystem` keys the boot reads (`[vmachine] device/fullscreen/fillobjmem/cdrom_mountpoint`, `[sound] card`, `[network] enable`).
25. ~~**libc import audit**~~ **RESOLVED** — the ~30 libc symbols (incl. glibc-internal `__strtod_internal`/`__strtol_internal`) plus pthread/dl/sockets/SMPEG are implemented in the HLE OS-shim (`port/src/traps.cpp`); a full boot into gameplay shows **0 unimplemented traps**.
26. ~~**Game data** — obtain the CD/ISO~~ **RESOLVED** — user provided the CD in `data/cd/`; runtime reads assets from there (`/mnt/cdrom` remapped) and from `data/game/` (extracted packs).

## Minor / deferred
18. Map loader `FUN_081c7a00` → actual file read (deferred).
19. Multiplayer scope — battle-only (tactical) vs. full strategic (`scenarioID = -1` + battle-map load suggests standalone tactical battles).
