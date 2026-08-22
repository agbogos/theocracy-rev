# MVOS API inventory (M0 deliverable)

Full demangled export/import inventory of `libmvos.so` and the game↔engine
boundary. This is the machine-generated reference the HLE trap table and
reconstructed headers are built from. **Regenerate** any time with
`tools/regen_api.sh` (see below).

## The demangling problem, solved

libmvos uses **GNU v2 mangling** (g++ 2.x), which modern `c++filt`/LLVM dropped
— it echoes the symbols back unchanged. So `tools/gnuv2_demangle.py`
reimplements the v2 grammar subset this binary uses (types, cv-qualifiers, `Q`
nested names, `t` templates, `N`/`T` backrefs, `G` value markers, operators,
vtable/RTTI forms). Result: **2400/2400 (100%)** of libmvos C++ exports demangle
cleanly.

Spot-checks:
```
Refresh__7cGD_X16RC10cRectangle        -> cGD_X16::Refresh(const cRectangle &)
__10cVOAButtonRC10cRectangleR5cFontPCc22...bN25UsP8cVObject
    -> cVOAButton::cVOAButton(const cRectangle&, cFont&, const char*, eVOAButtonTextAlignment, bool, bool, bool, unsigned short, cVObject*)
__pl__8cDayTimeT0                      -> cDayTime::operator+(cDayTime)
__opPc__7cString                       -> cString::operator char *()
__vt_12cData_Bitmap.10cMemBlock_       -> cData_Bitmap virtual table (base cMemBlock_)
```

## Generated artifacts (in `data/`)

| File | What | Rows |
|------|------|------|
| `data/mvos_exports.tsv` | every libmvos export: `addr<TAB>raw<TAB>demangled` | 2400 |
| `data/game_imports.tsv` | the game's undefined symbols = **the HLE boundary** | 232 |
| `data/mvos_api.json` | structured inventory: classes → {ctors, dtor, methods, operators, vtable/rtti flags, per-member addrs}, plus free functions & data globals | — |

## Engine shape (from `mvos_api.json`)

- **252 classes**, **131 polymorphic** (have a vtable), **1304 methods** total.
- **72 free functions** (e.g. `PutText(cGD&, cFont&, …)`, `Clip(...)`,
  `Print_Dec(long)`, the `VM_Get*` accessors).
- **184 data globals** — the singletons (`VVC`, `EnvSystem`, `TimerSystem`,
  `StdConv`, …) plus the C-named LFB blitter routines (`LFB16_Fill`,
  `LFB32_PutBitmap8`, …) and enum-return sentinels (`LDBRET_*`).

Biggest classes: `cGD` (46 methods — the software rasterizer base), `cVObject` (36 — UI widget base), the `cGD_LFB8/15/16/24/32` + `cGD_SFB8/16` blitters (~30 each), `cLineEditor`, `cVOAButton`, `cFile`, `cVVC`.

## The HLE boundary — game imports (232)

The game pulls in exactly **232 symbols**; this is the complete list of native entry points the emulator must provide.

- **191 MVOS calls** into **53 distinct engine classes**. Most-called:
  `cVOAButton` (14), `cFile` (13), `cVObject` (11), `cVOConsole` (7), `cGD` (6),
  `cTextFile` (6), then
  `cAnimBitmap`/`cAnimSkeleton`/`cThread`/`cDayTime`/`cScreen`/`cConsole` (5
  each). So the boundary is dominated by **UI widgets, file I/O, graphics
  device, and animation** — exactly the app-facing surface.
- **41 libc/runtime**: `malloc/free`, `memcpy`,
  `printf/sprintf/fprintf/puts/sscanf`, the `str*` family, `fopen/fclose`, the
  `pthread_*` mutex/key subset, `CopyMem`, `__builtin_vec_delete`,
  `exit/_exit/atexit/abort`, profiling (`monstartup/_mcleanup`), and
  glibc-internal
  `__strtod_internal`/`__strtol_internal`/`__write`/`__libc_init_first`. All
  implementable by behavior (the internal names just map to standard libc).

This 232-entry list is the M2 trap table. Note it is a strict subset of the 2400 engine exports, so the reconstructed headers (next step) cover it and more.

## Reconstructed headers — `include/mvos_api.hpp`

`tools/gen_headers.py` turns the JSON into a **C++ signature reference**: every engine class with its ctors/dtor/operators/methods, and — the useful part — **each method annotated with its file address** (`@0x…`; Ghidra address = file + `0x10000`). It doubles as the HLE implementation worklist. 220 classes + 32 template instantiations + 68 free functions + the singleton `extern`s.

```cpp
// ==== cThread [polymorphic, rtti] =====
class cThread {
    // +0x04 cStream vtable base; +0x08/+0x0c pipe fds; +0x10 running; +0x14 pthread_t.
public:
    cThread();   // @00095860
    mvret Launch();   // @000958c0  /*ret?*/
    ...
};
```

Honest limitations, all inherent to the symbol table (documented in the file banner, filled in as implementation proceeds from Ghidra):
- **`mvret /*ret?*/`** — GNU-v2 mangling omits return types. `mvret` is a
  placeholder typedef; grep `/*ret?*/` for the worklist.
- **`[polymorphic]`** flags a vtable exists, but *which* methods are virtual and
  their *slot order* aren't in the symbols — read Ghidra vtables.
- **Bases** are vtable-mixin hints only (23 classes; primary base + order not
  recoverable from symbols — e.g. `cString : cMemBlock_`, `cSoundCard_Linux :
  cThread`).
- **Templates/enums** (`tPoint<long>`, `cArray<…>`, `eBMType`) are referenced
  but not defined here; real definitions arrive with the layout work. So it's a
  declaration *reference*, not yet a standalone translation unit.
- **Field layouts** are spliced in only for the handful of hand-reversed classes
  (from `docs/structs`, `docs/subsystems`).

## Tooling

- **`tools/gnuv2_demangle.py`** — stdin `[addr] symbol` lines → stdout
  `addr<TAB>symbol<TAB>demangled`; coverage summary to stderr. Importable (`from
  gnuv2_demangle import demangle`). Reusable for the game binary's own
  RTTI/vtable symbols and any future `.so`.
- **`tools/build_api_inventory.py`** — demangled TSV → structured JSON
  (stdin→stdout).
- **`tools/gen_headers.py`** — JSON → `include/mvos_api.hpp` (stdin→stdout).
- **`tools/regen_api.sh`** — regenerates all `data/` artifacts **and** the
  header in one shot.

### Grammar coverage caveat
100% of libmvos and 100% of the game's *C++* imports resolve; the only pass-throughs are genuinely-unmangled C symbols (`__bss_start`, `__builtin_vec_delete`, glibc `__*_internal`), which are correct as-is. If a future binary (e.g. `theocracy.real`'s own exports, or the input plugins) surfaces a construct the parser doesn't handle, `gnuv2_demangle.py` reports it on stderr rather than failing — extend the grammar there.

## Status: M0 complete
Demangler ✅, inventory ✅, HLE boundary extracted ✅, header reference ✅ (`include/mvos_api.hpp`). Next is **M1** — the ELF loader + Unicorn bring-up (`docs/porting/macos-hle-emulator.md`), which needs no Ghidra and no game data. The remaining symbol-derived refinements (return types, vtable slot order, full inheritance, field layouts) are best recovered *incrementally in Ghidra as each subsystem is implemented*, not in a big upfront pass.
