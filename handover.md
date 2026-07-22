# Handover — Theocracy RE / macOS port

_Last updated: 2026-07-22._ Read this first, then `docs/README.md`.

## Goal
Make **Theocracy** (Philos Laboratories, 2000; Linux binaries in `data/cd/linux/`) run **natively on modern macOS** (Apple Silicon). Not a VM, not OS-level emulation shims, not a decompile-first rewrite — the game binary stays byte-for-byte intact and its i386 code runs under Unicorn.

### ⚡ Current architecture — guest-libmvos (PLAYABLE). Read `docs/porting/guest-libmvos.md`.
Map **both** `theocracy.real` **and** the real `libmvos.so` under Unicorn; HLE-only the finite OS/library boundary (libc / pthread / dl / sockets / SMPEG). The real engine runs, so the GUI/render/asset code is not reimplemented by hand. **Single-player is playable**: menu → Single Player → realm → units (select/move), diplomacy/war, save/load, cutscenes with video+audio; 0 unimplemented traps. Remaining work: `task_fifo.md`; manual QA: `user-test.md`.

**Pivot note.** The project originally pursued *pure-HLE native-replace* (run only the game, reimplement all of libmvos natively — `docs/porting/macos-hle-emulator.md`, M1/M2). That reached a live render loop but hit an unbounded GUI-reimplementation wall, so we pivoted (2026-07-22). The **"Done so far" and "M2 core" sections below are historical** — kept for the RE facts (ABI, boot, struct layouts) they contain, which still apply. `port/src/mvos.cpp`/`video.cpp`'s pure-HLE render code is left in tree but **not linked**.

## Why this is viable (all RE-confirmed)
- The game links only `libmvos.so` + libc; **every OS dep (X11, OSS, pthreads, fork, sockets, CD) sits behind the libmvos boundary.**
- The game imports exactly **232 symbols** = the entire native surface to implement.
- **libmvos owns `main()`** (game imports it) → our native runtime *becomes* `main()` and controls the whole boot. Boot sequence fully decompiled — `docs/subsystems/application-bootstrap.md`.

## Repo layout
- `linux/` — the game binaries (see `docs/overview.md` for the inventory). **No game data** — it's on the CD, installed by `inst.linux`. A CD/ISO is still needed for anything past M1 (open blocker).
- `docs/` — the canonical knowledge base. Update it; don't duplicate into ad-hoc notes. Start at `docs/README.md`.
- `tools/` — reusable scripts (see below).
- `data/` — generated API artifacts (regenerate, don't hand-edit).
- `include/mvos_api.hpp` — generated signature reference / HLE worklist.

## Done so far
- **All recon complete** on the 3 boundaries: game↔engine ABI, engine↔OS platform layer, engine↔driver (video/input plugin). See the `docs/subsystems/*` and `docs/porting/vvc_x-backend.md`.
- **M0 done**: wrote a from-scratch **GNU v2 demangler** (`tools/gnuv2_demangle.py` — modern c++filt/Ghidra can't do pre-3.0 mangling), built the inventory (252 classes / 1304 methods), extracted the **232-symbol trap boundary** (`data/game_imports.tsv`), and generated `include/mvos_api.hpp` (each method tagged with its file address = the implementation worklist). Details: `docs/reference/mvos-api-inventory.md`. Regenerate everything: `sh tools/regen_api.sh`.

- **M1 done**: `port/` is a working C++17 + Unicorn 2 host. It maps the ELF, traps all 232 imports (95 more JMP_SLOT/GLOB_DAT resolve to game-local exports), runs the 215 `.ctors` under emulation, calls `Init__12cApplication`, and all 9 subsystem flags go `0 → 1` — we execute the game's own code and reach its first callback. Build: `cmake -S port -B port/build && cmake --build port/build`; run: `DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc linux/theocracy.real`. Full writeup + confirmed ELF facts + M2 worklist: `docs/porting/m1-loader.md`. Reusable ELF fact-check: `tools/elf_facts.py` (needs `pyelftools`); copy-reloc inventory: `data/theocracy_copyrelocs.tsv`.

## Game data — UNBLOCKED
CD is in `data/cd/` (user provided). The `.pck` packs are gzip-wrapped **PHLS** archives — cracked and extracted byte-exact by `tools/phls_extract.py`: `tdat.pck` → **7191 files** under `data/game/data/` (the Linux game data root: anim, map, locale, menu, sounds, scenario…). `tex.pck` is the Windows installer payload (skip). Format doc: `docs/reference/phls-format.md`. **No installed copy / Debian VM needed.** Two data caveats surfaced, one now solved: (a) `.cfg`/`.txt`/`.idx` files are **encrypted** behind an `RSA4096` header marker (binary assets plaintext) — **SOLVED**: symmetric XOR with keys `"theocracy sux"` (period 13) + `"mutant technology"` (period 17) over the post-header body (user supplied the recovered `XorBuff` → `tools/crypt/TheocracyEncDec.cpp`; ported to `tools/theocracy_crypt.py`, verified byte-exact; `phls_extract.py --decrypt` applies it). M2 ports `XorBuff` into the HLE `cTextFile`. (b) `mvos.cfg` isn't in the packs (installed by `inst.linux`) — still open. See `docs/open_questions.md` (#27 resolved, #28 open).

## M2 core — IN PROGRESS
Native MVOS layer landed in `port/` (`mvos.{hpp,cpp}` + multi-region traps in `machine.*`). Details: `docs/porting/m2-core.md`. Done: **vtable synthesis** — the 79 `R_386_COPY` relocs now processed; 34 `__vt_*` tables (157 slots) filled with per-slot traps so guest virtual dispatch lands in `dispatch_vtable` (logs `[vtable] TODO name[slot]`); 10 pointer singletons backed by zeroed guest objects; first handlers (`cData_Bitmap`/`cData_AnimBitmap` ctors, `cMemBlock_::IsValid`, `GetBoundingBox`). Fault diagnostics now report `eip`+fault-addr. Result: `.ctors` faults **2→1**, Init still clean, 9 flags still 0→1. The `0x817e9d0` pointer-sprite ctor is fixed.

### cTextFile done (this session)
Reverse-engineered the real `cTextFile` in libmvos (`OpenR` `0x64f70`, `ReadLine` `0x65070`, `CountLines` `0x65110`) and reimplemented it natively over an in-memory **decrypted** body (shares the `RSA4096` XOR from `tools/theocracy_crypt.py`). Guest `data/…` paths resolve under `$THEOC_DATA` (default `data/game`). `Fatal(char*)` now prints each distinct message once, then continues (bring-up visibility). Result: game now reads+parses its real config files — `sscanf` calls jumped to 8430, execution reaches the **unit-animation loader**. Implemented imports 4→13.

### sscanf + MVOS method batch done (this session)
1. Full native `sscanf` (`traps.cpp do_sscanf`: `%d i u x X o p f e g s c`, scansets, width, `*`, length mods). Keystone — #1 unimplemented AND the cause of the 169 `Nincs -1` `.spn` asserts (loader parses via `sscanf`). → 0 Fatals.
2. MVOS method batch from real libmvos bodies: `cRandom::Rnd` (LCG `state*0xFFFFFFF1+0x7FFFFFFF`, `/4294967295.0`), `cData_Sample`/`Palette`/`Font` ctors + unified `cData_Bitmap`/`AnimBitmap` (lazy-cMemBlock: primary vtbl +0x08, payload +0x1c, base fallback), `cNode`/`cHNode::UnLink`, `cList::UnLinkList`, `cLocaleEntry`.
3. **x87 float returns**: `Machine::return_double` stashes the value and redirects EIP into a guest `FLD [scratch]; RET` stub (real x87 does the FPU push). Stable across 2685 `Rnd` calls. Reusable for any float-returning MVOS method.
Result: UNIMPLEMENTED calls 5048→**194** (12 low-freq imports left), implemented 19 (27k calls), 0 Fatals, Init clean.

### Last ctor fault RESOLVED (this session)
Game-side Ghidra pass debunked the M1 guess: `DAT_085bf980` is **not** a mysterious libmvos system global — it is `cData_Font("data/fonts/small_1pix.mft")` built by `FUN_08141f30` (renamed **`RegisterGlobalResources`**, ctor #194, which we already run). `cData_Font : cAnimBitmap`. The fault peeled off in 3 stages: (1) null payload vtable — already fixed by the prior batch's `make_descriptor`; (2) `+0x41c` — sprite ctor reads the unloaded `cAnimBitmap` frame-header ptr `+0x40`, fixed with a shared zeroed **`NULL_FRAME`** page every `cData_*` descriptor points `+0x40` at (unloaded → size 0, no fault; real load overwrites it); (3) `accessing 0x0` — **`strtok` was unimplemented**, the `rd.txt` parse deref'd its null result, fixed by a native in-place `strtok`. Result: **215/215 ctors clean, 0 faults, 0 Fatals**, Init reached, 9 flags 0→1. Two `[vtable] TODO` no-op hits remain (the lazy-load seam).

### Video backend STARTED — first pixels (this session)
`port/src/video.{hpp,cpp}` = a native **SDL2** backend that replaces the whole `libmvos_vvc_x.so` plugin (RGB565 framebuffer + window + streaming texture; `present()` = update-texture + present + pump). The video boundary is **direct import traps, not virtual dispatch** (no `__vt_*cVVC*`/`__vt_*cGD*` in copyrelocs). First handler: **`cVVC::OpenDisplay(cVModeRequest&)`** (`mvos.cpp`) — request layout from libmvos `0x95ce0` is `+0 w, +4 h, +8 depthCode`; opens the SDL window, writes w/h/depth back into the guest `cVVC` at libmvos's offsets (`+0x20/+0x24/+0x1c`), returns success. **Verified** with `THEOC_VIDEO_TEST=1`: reads `VVC` singleton (`0x08598cec`→`0x51002000`), builds an 800×600/depth-5 request, invokes the `OpenDisplay` trap → window opens + RGB565 gradient renders. SDL2 links via `-lSDL2` (header/lib in `/opt/homebrew`). Env: `THEOC_VIDEO_TEST`, `THEOC_START`, `THEOC_VIDEO_HOLD=<s>`.

**`cApplication::Start` progressed a long way (this session).** Decompiled `Start` (`0x08144650`, top-level state machine). The blocker was **singleton virtual dispatch** — the game calls polymorphic methods on framework singletons whose class vtables aren't copy-reloc'd, and our singletons were zeroed (null vtable) → first virtual call faulted (`Start+0x36`, `IPCSystem->vtbl[3]`, the localhost:5043 single-instance lock). **Landed a general mechanism** (`mvos.cpp wire_singleton_vtables`): reserved 1024-slot VT-trap pool; every singleton's backing object gets a synth 32-slot vtable (virtual calls → `dispatch_vtable`); `hook_vslot()` for native overrides. Overrides: `IPCSystem->vtbl[3]`→fake lock, `VCD+0xc`→synth driver table. `Start` now runs through IPC lock, sound-channel setup, CD thread ctor, `ActivateScreen` (stub → non-fatal "Unable to activate screen"), **into main-menu `cVOAButton` construction** (`0x816e3d0`) — which walls on measuring text width from a **real font** (null font ptr / needs `font+0x40` decoded glyph table). **From here the road converges on the asset-decode + `cGD` render layer, not more singleton stubs.**

**RENDER LOOP LIVE — the game drives the SDL window (this session).** Reimplemented the three render-boundary imports natively (`mvos.cpp`, from libmvos originals): `ActivateScreen__10cIntuition` (`0x9d830` — game's `cScreen` header IS a `cVModeRequest` w/h/depth@+0/+4/+8, root cVObject@+0x14; opens the SDL window + stores screen at `Intuition+0x24`, the active screen the render path reads), `BeginRefresh__7cScreen` (`0x9d2a0` → `Video::pump()`), `EndRefresh__7cScreen` (`0x9d2d0` → `Video::present()`; real one does `PaintTree(root, *(VVC+0x14))` + `SwapBuffers`). Plus a `cLocaleEntry` text placeholder (ctor points `+0x10` at the key string → non-null button labels). **`cApplication::Start` now runs end-to-end, NO fault**: boot → `Init` → `MainMenu_Run` builds all 11 buttons → `ActivateScreen` opens the window → `BeginRefresh`/`EndRefresh` loop presents frames, waiting on the event pipe (15s emulation timeout, clean). Placeholder gradient bg drawn on activation. `THEOC_START=1`.

## Status (2026-07-22) — game is playable (guest-libmvos)

Dual-image emulator: `theocracy.real` + real `libmvos.so` under Unicorn, HLE-only
OS boundary, SDL present, Intuition input pipe. Full milestone log (G1–G11) in
`docs/porting/guest-libmvos.md`. **User-verified:** start a game, move units,
declare war, save/load. Landed since the pivot: menu click→realm, mouse via the
`Intuition+0x28` pipe, `eKeyCode` keyboard + text fields, gameplay audio
(`/dev/dsp`→SDL), setitimer→TimerSystem, **real game cursor sprite**, MPEG
cutscenes via libav **with audio** (swresample→mixer). 0 unimplemented traps.

Build (needs `unicorn SDL2 avformat avcodec avutil swscale swresample` from brew):
```sh
cmake -S port -B port/build && cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc     # defaults to data/cd/linux/*
```

### Next (see `task_fifo.md`, top = first)
1. **Auto `R_386_COPY` singleton sync** in the linker (drop the manual sync in `main.cpp`)
2. **abort/Fatal policy** — loud-abort mode so latent faults surface
3. **Province-view perf** (last open functional bug; also slow on the Win VM)
4. Long-session stability, breadth (full UI surface, multiplayer)
## Gotchas / conventions (will bite you)
- **Shell is zsh with `noclobber` ON** — use `>|` to overwrite files in Bash calls, or redirects fail with "file exists".
- **Ghidra is via MCP, ONE binary at a time**; the user switches the open file manually — ask them. Load the ghidra MCP tool schemas via ToolSearch (`select:mcp__ghidra__...`) before calling.
- **Addresses:** libmvos Ghidra base `0x10000` (file addr = Ghidra − `0x10000`); game base `0x08048000`. The `@0x...` in `mvos_api.hpp` are **file** addresses.
- The Ghidra DBs already carry renames/plate comments from prior sessions (incl. this one's on `main`/`OpenSubsystems`/`SetVideoMode`). Build on them; write findings back as comments/renames AND mirror durable results into `docs/`.
- Symbols in the Ghidra DB are **still mangled** (Ghidra can't demangle v2) — use `tools/gnuv2_demangle.py` or `data/mvos_exports.tsv` to read them.

## Deferred / for later (don't do now)
- **Ghidra bulk C export** (GUI: File → Export Program → C/C++) for `libmvos.so` gives all decompiled *bodies* at once — the complement to the signature table, for filling `/*ret?*/` return types and real logic. **Only useful at M2+** (implementing trap handlers), not M1. Ask the user to produce it when we get there.
- Return types, vtable slot order, full inheritance, field layouts: recover **incrementally in Ghidra as each subsystem is implemented**, not in a big upfront pass.
- `LoadDevicePlugins` (`0xa4990`) internals; `eBMType` enum full value set; game-side `cThread` subclass census; `inst.linux` unpack format (for a native CD extractor). All in `docs/open_questions.md`.

## Legal note
Personal-use RE of the user's own copy is fine; *distributing* a reconstructed port sits in the same gray zone devilutionX lives in (Philos defunct; rights via Ubisoft in limbo).
