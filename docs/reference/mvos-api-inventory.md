# MVOS API inventory

Full demangled export/import inventory of `libmvos.so` and the game↔engine
boundary. This is the machine-generated reference the HLE trap table and the
reconstructed headers are built from. Regenerate it any time with
`tools/regen_api.sh` (see below).

## The demangling problem

libmvos uses GNU v2 mangling (g++ 2.x), which modern `c++filt`/LLVM dropped
— it echoes the symbols back unchanged. So `tools/gnuv2_demangle.py`
reimplements the v2 grammar subset this binary uses (types, cv-qualifiers, `Q`
nested names, `t` templates, `N`/`T` backrefs, `G` value markers, operators,
vtable/RTTI forms). All 2400 of libmvos's C++ exports demangle cleanly.

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

- 252 classes, 131 of them polymorphic (they have a vtable), 1304 methods.
- 72 free functions (e.g. `PutText(cGD&, cFont&, …)`, `Clip(...)`,
  `Print_Dec(long)`, the `VM_Get*` accessors).
- 184 data globals — the singletons (`VVC`, `EnvSystem`, `TimerSystem`,
  `StdConv`, …) plus the C-named LFB blitter routines (`LFB16_Fill`,
  `LFB32_PutBitmap8`, …) and enum-return sentinels (`LDBRET_*`).

Biggest classes: `cGD` (46 methods, the software rasterizer base), `cVObject`
(36, the UI widget base), the `cGD_LFB8/15/16/24/32` and `cGD_SFB8/16` blitters
(~30 each), then `cLineEditor`, `cVOAButton`, `cFile` and `cVVC`.

## The HLE boundary — game imports (232)

These are the complete set of native entry points the emulator has to provide,
and they are a strict subset of the 2400 engine exports — nothing the game
imports resolves anywhere but libmvos and libc.

- 191 MVOS calls into 53 distinct engine classes, dominated by UI widgets, file
  I/O, the graphics device and animation — the app-facing surface. Most-called:
  `cVOAButton` (14), `cFile` (13), `cVObject` (11), `cVOConsole` (7), `cGD` (6),
  `cTextFile` (6), then
  `cAnimBitmap`/`cAnimSkeleton`/`cThread`/`cDayTime`/`cScreen`/`cConsole` (5
  each).
- 41 libc/runtime: `malloc/free`, `memcpy`,
  `printf/sprintf/fprintf/puts/sscanf`, the `str*` family, `fopen/fclose`, the
  `pthread_*` mutex/key subset, `CopyMem`, `__builtin_vec_delete`,
  `exit/_exit/atexit/abort`, profiling (`monstartup/_mcleanup`), and
  glibc-internal
  `__strtod_internal`/`__strtol_internal`/`__write`/`__libc_init_first`. All
  implementable by behavior (the internal names just map to standard libc).

## Reconstructed headers — `include/mvos_api.hpp`

`tools/gen_headers.py` turns the JSON into a C++ signature reference: every
engine class with its ctors, dtor, operators and methods, each annotated with
its file address (`@0x…`; Ghidra address = file + `0x10000`). 220 classes, 32
template instantiations, 68 free functions and the singleton `extern`s. The
addresses are what make it usable as an implementation worklist as well as a
reference.

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

What the symbol table cannot give, all of it noted in the file's own banner:
- `mvret /*ret?*/` — GNU-v2 mangling omits return types. `mvret` is a
  placeholder typedef; grep `/*ret?*/` for the worklist.
- `[polymorphic]` flags a vtable exists, but *which* methods are virtual and
  their *slot order* aren't in the symbols — read Ghidra vtables.
- Bases are vtable-mixin hints only (23 classes; the primary base and its order
  are not recoverable from symbols — e.g. `cString : cMemBlock_`,
  `cSoundCard_Linux : cThread`).
- Templates and enums (`tPoint<long>`, `cArray<…>`, `eBMType`) are referenced
  but not defined, so the file is a declaration reference rather than a
  standalone translation unit.
- Field layouts are spliced in only for the handful of hand-reversed classes,
  from `docs/structs` and `docs/subsystems`.

## Tooling

- `tools/gnuv2_demangle.py` — stdin `[addr] symbol` lines → stdout
  `addr<TAB>symbol<TAB>demangled`; coverage summary to stderr. Importable (`from
  gnuv2_demangle import demangle`). Reusable for the game binary's own
  RTTI/vtable symbols and any future `.so`.
- `tools/build_api_inventory.py` — demangled TSV → structured JSON
  (stdin→stdout).
- `tools/gen_headers.py` — JSON → `include/mvos_api.hpp` (stdin→stdout).
- `tools/regen_api.sh` — regenerates all `data/` artifacts and the
  header in one shot.

### Grammar coverage caveat

All of libmvos and all of the game's *C++* imports resolve; the only
pass-throughs are genuinely unmangled C symbols (`__bss_start`,
`__builtin_vec_delete`, glibc `__*_internal`), which are correct as they stand.
On a construct the parser does not handle — from `theocracy.real`'s own exports,
say, or the input plugins — `gnuv2_demangle.py` reports it on stderr instead of
failing, and the grammar gets extended there.

## What is left

Everything the symbol table alone can give is generated: the demangler, the
inventory, the HLE boundary and the header reference. The refinements it cannot
give — return types, vtable slot order, full inheritance, field layouts — come
out of Ghidra a subsystem at a time, as each is read, and land in the doc that
owns that subsystem rather than here.
