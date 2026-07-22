# M1 — ELF loader + Unicorn bring-up (DONE)

> **⚡ SUPERSEDED (2026-07-22) — pivoted to guest-libmvos.** M1 was the pure-HLE
> loader (single image: `theocracy.real` only, all 232 libmvos imports trapped).
> The current path uses a **dual-image** linker (`port/src/guestlink.cpp`) that
> maps `theocracy.real` *and* real `libmvos.so`, and traps only the OS boundary
> — see **[guest-libmvos.md](guest-libmvos.md)**. The ELF facts, trap mechanism,
> and Unicorn setup below carried over; the single-image approach did not.

The first executable milestone of the [macOS HLE emulator](macos-hle-emulator.md):
load `theocracy.real`, wire the 232-import trap boundary, run the game's own
C++ global constructors under emulation, call `cApplication::Init`, and read
back the 9 subsystem flags. **Result: all 9 flags go `0 → 1`, set by the game's
own `Init` — we execute the game's code and reach its first callback.** No
Ghidra and no CD data were needed.

Code lives in [`port/`](../../port) (C++17, Unicorn 2). Build & run:

```sh
brew install unicorn cmake          # one-time
cmake -S port -B port/build && cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc linux/theocracy.real
```

## Confirmed ELF facts (`theocracy.real`)

Verified against the binary (not just inherited from docs):

| Fact | Value |
|------|-------|
| Type / machine | `ET_EXEC` / `EM_386` |
| Entry (`_start`, unused by HLE) | `0x08050110` |
| PT_LOAD 0 (text+rodata) | va `0x08048000`, filesz=memsz `0x47e565`, `R-X` |
| PT_LOAD 1 (data+bss) | va `0x084c7580`, filesz `0x0d0ae4`, memsz `0x1ba978` (bss `0xe9e94`), `RW-` |
| `.ctors` | `0x08597400`, 0x364 bytes = 217 words → **215 constructors** |
| `.dynsym` | 348 defined exports + **232 UND imports** |
| Callbacks | `Init__12cApplication` `0x8144600`, `Start__12cApplicationiPPc` `0x8144650`, `RollingDemoFrame__Fv` `0x8063540` (all MATCH) |

### Relocations (correction to earlier notes)

There is **no `.rel.dyn`**; the sections and i386 types are:

| Section | Count | Type | Loader action |
|---------|-------|------|---------------|
| `.rel.plt` | 265 | `R_386_JMP_SLOT` (7) | GOT slot ← trap addr (UND) or local sym value |
| `.rel.got` | 62 | `R_386_GLOB_DAT` (6) | same rule |
| `.rel.bss` | 79 | `R_386_COPY` (5) | framework singletons/vtables — **skipped in M1** |

Of the 327 JMP_SLOT+GLOB_DAT relocs, **232 resolve to trap stubs** (the UND
imports) and **95 resolve to the game's own exports** (vague-linkage/template
code the game references through the PLT — not a trap). ET_EXEC at a fixed base
means there are zero `R_386_RELATIVE`/`R_386_32` base relocations.

### The 9 subsystem flags = static `cApplication` members

They are `static` members of `cApplication`, `R_386_COPY`-relocated from libmvos
into the game's `.bss`. Since we don't load libmvos, `.bss` starts zeroed and
`Init` writes them. Verified addresses:

| Flag | Address | Flag | Address |
|------|---------|------|---------|
| Sound | `0x085983cc` | Network | `0x0859848d` |
| Video | `0x085986ec` | Pointer | `0x0859849c` |
| Mouse | `0x08598b90` | Timer | `0x08598080` |
| Keyboard | `0x0859847c` | Intuition | `0x085986dc` |
| Redbook | `0x08598c60` | | |

Related COPY'd singletons at fixed `.bss` addrs (for M2): `EnvSystem`
`0x08598370`, `SystemMemory` `0x08598404`, `Intuition` `0x08598454`, `IPCSystem`
`0x08598338`, `VVC` `0x08598cec`, `SoundCard` `0x08598d0c`, `VCD` `0x085984ac`,
`VKeyboard` `0x08598b58`, `VMouse` `0x08598c3c`, `LocaleDataBase` `0x08598c4c`,
plus `Palette15/16/24`, `_IO_stdout_`, `__environ`, and ~40 `__vt_*`/`__ti*`.
Full list: `data/theocracy_copyrelocs.tsv` (regenerate via the fact-check in
`tools/`). These are the vtable/singleton data M2's HLE must supply.

## How M1 works

- **No host↔guest 1:1 mapping yet.** M1 runs entirely inside Unicorn's own
  guest address space (`uc_mem_*`); the low-4GB aliasing trick / shrunk
  `__PAGEZERO` is an M2 concern (zero-copy guest pointers).
- **Guest memory map** (`port/src/machine.hpp`): image at its native VAs; trap
  window `0x77000000` (1 byte/import), heap `0x60000000` (128 MB bump alloc),
  stack top `0x70000000` (2 MB), scratch `0x50000000`.
- **Trap mechanism:** every UND import gets a unique 1-byte address in the trap
  window; JMP_SLOT/GLOB_DAT GOT slots are pointed there. A scoped
  `UC_HOOK_CODE` over the window fires when the guest jumps in, reads the return
  address off the stack, dispatches the native handler, then emulates a cdecl
  `ret` (args left on the stack — caller-cleanup; `this` is the first stack
  arg). Return value → `EAX`.
- **call-guest primitive:** push cdecl args + a `STOP` sentinel return address,
  `uc_emu_start(fn, until=STOP)`; a clean return lands on `STOP`, a timeout
  leaves `EIP` elsewhere (detected). Used for each `.ctors` entry and `Init`.
- **`.ctors` order:** GCC 2.x `[-1, c0..ck, 0]`; run high→low (reverse), skip
  header + null terminator.
- **Native handlers implemented for M1** (`port/src/traps.cpp`): a bump heap
  (`malloc`/`calloc`/`realloc`/`free`), `mem*`/`str*`, `printf`-family +
  `sprintf`/`__write`, and no-op startup stubs. Everything else logs `TODO`
  once and returns 0 — that log is the M2 worklist.

## Result

```
.ctors done: 213 ok, 0 no-return, 2 faulted
Init returned
Sound … Intuition : 0 -> 1   <- all nine set by Init
```

The game's own code genuinely executes: during `.ctors` it emits its own
`printf` diagnostics (`MAX XXXX…:======= 60` / `GetMax: …`).

## Loose ends → M2

- **2 ctors fault** (read unmapped memory): `#178 @0x817e9d0`, `#213 @0x8064bd0`.
  Both dereference a null returned by an unimplemented data-descriptor ctor
  (`cData_Bitmap`/`cData_AnimBitmap` returning 0) — they clear once those
  classes get real HLE. Caught and reported; they don't block Init.
- **M2 worklist, by call frequency** (what ctors + Init actually exercise):
  `cRandom::Rnd` (2685), `cLocaleEntry(const char*)` (1755),
  `cData_Bitmap(const char*,bool)` (1397), `cData_AnimBitmap` (486), then the
  locale/config text-file path — `cTextFile::OpenR/ReadLine/Close` + `sscanf`
  (338 each) + `IdentifyFileSystem` (169) — and `cData_Sample`/`cData_Palette`/
  `cData_Font` ctors, `cList/cNode::UnLink`, `Fatal`. This says the boot so far
  is dominated by RNG seeding, locale-string construction, asset *descriptor*
  construction, and text-file (locale/config) parsing.
- Next code step is M2's own milestone: real `cSystemMemory` + strings/
  containers + file I/O + the `mvos.cfg`/`EnvSystem` parse, then video/input to
  first pixels (**needs CD data**).
