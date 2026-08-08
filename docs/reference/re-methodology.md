# How to read these binaries without being wrong

The failure modes this project has actually hit while reverse-engineering
`libmvos.so` and `theocracy.real`, each with the incident that produced it and
the check that would have caught it. Read this before lifting an address, an
offset, or a struct layout out of a decompile.

It exists because a documented wrong address is worse than a missing one: the
2026-07-26 audit re-checked every address `docs/` cited and found **five wrong
claims**, and every single one came from one of two mechanical failure modes
described below — not from misunderstanding the code.

---

## 1. Say which address space you mean

Two binaries, two bases, and one of them has an image-base offset that makes a
wrong address look plausible:

| Binary | Ghidra base | Conversion |
|---|---|---|
| `libmvos.so` | `0x00010000` | file offset = Ghidra − `0x10000` |
| `theocracy.real` | `0x08048000` | ET_EXEC, fixed — Ghidra address *is* the runtime address |

Docs follow the README convention (Ghidra addresses of the binary the doc
covers); the host's log labels are `mvos+0x…` (**file** offsets, since that is
what `guestlink` maps) and `game 0x08…`. `include/mvos_api.hpp`'s `@0x…`
annotations are file offsets too.

**The incident.** `docs/` recorded libmvos `main` at file `0x851e0` for months.
The true file offset is `0x951e0` — exactly `0x10000` low, i.e. someone
subtracted the base twice or copied a Ghidra address into a file-offset slot.
The wrong value is not obviously wrong: it lands inside a real function. Settled
mechanically against `data/mvos_exports.tsv`.

**The check.** Any address that crosses from a decompiler into a document or
into host code should be reconciled against the export table, not retyped.

## 2. Offsets lifted from a decompiled `TYPE *` are scaled

The decompiler prints pointer arithmetic in units of the pointee. An offset read
off an `int *` parameter is **four times** the byte offset you want.

**The incident.** `simulation-step.md` gave a province sub-object as `+0x103a1`,
taken from `param_1 + 0x103a1` on an `int *`. The real byte offset is
`0x103a1 × 4 = 0x40e84`, and the raw instruction says so outright:

```
081d69f3  ADD EBX, 0x40e84
```

Corroboration was available without the disassembly too: `0x40e84` sits `0x3d6`
past the province owner byte at `+0x40aae`, in the same header region, whereas
`0x103a1` as a byte offset lands nowhere related.

**The check.** Note the pointer type before copying an offset. Offsets written
as `*(byte *)((int)p + N)` are *already* bytes and are safe — that is exactly
why the owner offset `+0x40aae` in the same function was always correct while
its neighbour was not. When a value is load-bearing, read the instruction.

## 3. A cited address may be a fragment, not an entry point

**The incident.** `vvc_x-backend.md` cited `LoadDevicePlugins` at `0xa49a0`. The
real entry is `0xa4990` — the address `OpenSubsystems` actually calls (from
`0xa4f73`), and the one carrying the "already loaded?" guard. `0xa49a0` has no
callers at all, only a single `.eh_frame` DATA xref. The Ghidra label sat on the
wrong address too, so the DB corroborated the error instead of catching it.

**The check.** Before citing an entry point, ask for its callers
(`get_xrefs_to`). A real function has code xrefs; a fragment has `.eh_frame`
DATA refs and nothing else.

## 4. Ghidra mis-flags g++ 2.95 tail-call thunks as non-returning

This is the highest-impact tooling problem in the project, because **it fails
silently**: the analyzer marks a thunk "No Return", and every caller is then
truncated at its first call to that thunk. What you get is a plausible,
complete-looking decompilation that is simply missing everything after the first
thunk call. Nothing warns you.

`tools/ghidra/FixBogusNoReturn.java` clears the bogus flags (and the
fall-through overrides they stamp on call sites). Run in 2026-07-26 it
un-flagged **495** functions in `libmvos.so` and **277** in `theocracy.real`.

Two things about the repair are worth knowing, both learned the hard way:

- **Clearing the flags does not re-merge function boundaries.** Split functions
  persist, with a duplicate `FUN_*` left at the old truncation point — `main`
  still reports a 48-byte body against its real `0xfc`. So **reported body
  extents remain unreliable even after the repair**, and that is precisely how
  §3's fragment address happened.
- **The MCP's `get_function_by_address` matches entry points only.** Any
  mid-body address answers "no function at address" — mid-`main` does too. This
  was briefly written up as evidence of live damage in the DB. It is a tool
  artifact and means nothing.

A related, smaller artifact: constructors that inline `Alloc`+`Lock` — both of
which call the genuinely-noreturn `Fatal` — get mis-flattened so the happy path
reads as though it always aborts. Read those as "assert, then continue".

## 5. Disassembly is immune to §2 and §4; use it for load-bearing claims

Both failure modes above are decompiler artifacts. The instruction stream has
neither. Every claim in `docs/` that something depends on has been cross-checked
against disassembly, and the hot paths (boot, render, input, the allocator)
additionally carry runtime proof — they demonstrably work, which is a stronger
statement than any static read.

That asymmetry is also how to scope an audit: **cold paths that runtime never
exercises are where a wrong layout hides.** Mechanically-derived facts (the trap
boundary, the copy-reloc inventory, `tools/elf_facts.py` output) need no
re-checking at all.

**A third artifact of the same family: `ROUND()` is not rounding.** Ghidra prints
every x87 float→int conversion as `ROUND(...)`, which reads as round-to-nearest.
g++ 2.x implements a C cast to integer by *changing the rounding mode first* —
`fnstcw` / `mov bh,0xc` (RC = 11, toward zero) / `fldcw` / `fistp` / restore — so
it is **truncation**, and the decompiler shows none of that. The difference is
not cosmetic: on the mana gauge (`0x0815a0a0`) the expression approaches 11 from
below without reaching it, so truncation keeps a sprite index inside `0..10`
while round-to-nearest would drive it to `-1` above 21000 mana. An
out-of-bounds-array finding was written up on the strength of the decompiler's
`ROUND` and then deleted once the bytes were read
([../subsystems/mana-and-sacrifice.md](../subsystems/mana-and-sacrifice.md), "A
note on `ROUND`"). Any threshold, index or comparison that depends on a
float→int conversion needs the instruction stream.

## 6. Guessed struct layouts are this port's dominant bug class

Not typos, not misread logic — ABI contracts assumed rather than read. Four
independent instances, each of which cost a debugging session:

| What was assumed | What it actually is | How it presented |
|---|---|---|
| Linux/i386 `struct stat` is ~96 bytes | Exactly **88**, `st_size` at `+0x2c` | `__xstat` wrote 8 bytes past a caller's stack local, zeroing the saved EBP and return address; the victim `ret`-ed to 0 several frames from the damage |
| Keyboard driver's next-event struct is `{count, key}` | `{keycode, flags}`, where `flags & 1` means "clear the key matrix" and is tested *before* the `keycode == 0` exit | Stale odd flags from one keypress wedged the menu in an infinite guest loop |
| `cSprite`'s save/restore is single-buffered | Two slots, one per buffer; `AfterSwapBuffer` restores through the *swapped* pointer | Cursor smeared its whole path across static screens |
| `cSoundCard_Linux`'s ctor ends `cThread::Launch(this)` | `Launch(this + 4)` — `cThread` is a *secondary* base at `+4` | Would misplace every `cThread` field read off a sound card (fds at `+0x0c`/`+0x10`, not `+0x08`/`+0x0c`) |

The pattern: a plausible layout produces plausible behaviour for a while, then
fails somewhere structurally distant from the assumption. When implementing an
OS or engine ABI, read the real definition or the real ctor; do not infer the
shape from how the caller seems to use it.

## 7. Evidence discipline: measure the running system, not the file

The netgame map-selection crash took three wrong diagnoses before an instrument
settled it. Each wrong one was stated more confidently than its evidence
supported, and each is a distinct error worth naming:

1. **A stack slot was read as a return address.** The "call site" came from
   `[ESP+0x18]`, six words into a raw fault dump. The actual return slot held a
   stack pointer, not code. *Fix:* walk the EBP chain and print a labelled
   backtrace instead of dumping 16 raw words for a human to pattern-match.
2. **A GOT value was read from the file on disk rather than from guest memory.**
   The claim "the slot for `__7cDirentPCc` is 0" was never measured on the
   running system. It was also refutable from existing output: `guestlink`
   prints `[link] unresolved strong UND`, and that line appears in no run.
3. **A truncated grep was taken as proof of absence.** "libmvos exports only the
   other `cDirent` overload" came from output that got cut off. It exports both
   (`__7cDirentPCc` at `0x4c030`, `Open__10cDirectory` at `0x4bab0`) perfectly
   well. A grep that gets cut off is not evidence of absence.

What actually closed it was building two instruments: a **zero-GOT scan** that
walks every `JMP_SLOT`/`GLOB_DAT` slot after linking and reports any still
holding 0 (result: none, killing the GOT theory outright), and a **32-entry
basic-block ring** dumped on fault, which showed that the address under
suspicion was a function *epilogue* rather than a call site — reframing the
whole problem as "the function returned to 0". See
[porting/diagnostics.md](../porting/diagnostics.md).

**Generalisable:** `eip=0` with `EBP=0` is a **smashed frame, not a null call** —
nothing has pushed a frame pointer yet. Look for who wrote past a buffer, not
for an unresolved symbol.

## 8. Reading the symbols at all

`libmvos.so` is g++ 2.95 with **GNU v2 mangling**, which modern `c++filt` and
LLVM dropped — they echo the symbol back unchanged, which reads like success.
Ghidra cannot demangle it either, so the Ghidra DBs show mangled names.

Use `tools/gnuv2_demangle.py` (a from-scratch v2 grammar implementation, 100% of
the 2400 libmvos exports) or look the symbol up in `data/mvos_exports.tsv`.
Details: [mvos-api-inventory.md](mvos-api-inventory.md).

Note also what the mangling *cannot* tell you: **return types are not encoded**
(hence `mvret /*ret?*/` in the generated header), and neither is virtual-ness or
vtable slot order. Those come from reading Ghidra vtables, not from symbols.

## 9. Hungarian is a landmark

Philos Laboratories was Hungarian, and the assert strings are too. They are
useful twice over — as a signal that you are in engine-original code, and as
runtime markers to grep the log for during bring-up:

| String | Where | Meaning |
|---|---|---|
| `Ezt nem kene!` | `cNode` copy-ctor | "This shouldn't be done!" |
| `Nincs -1` | `.spn` sprite loader | "There is no -1" — the terminator check |
| `netgame vege` | netgame teardown | "netgame end" |
| `### Most ki kene lepni ###` | win/lose check in `SimulationStep` | "should exit now" |
| `hmm..keves a map :-o` | `NetGame_InitBattle` | "hmm.. too few maps" |

The English strings carry the same fingerprint (`"You can run only one Theocracy
in the same time!"`, `"Inint menu buttons begin ..."`, `"Useing default video
device xf86."`) and are equally good grep anchors.

---

## 10. libmvos vtables are zeros in the file — apply `.rel.rodata`

Reading a vtable straight out of `libmvos.so.0.9` gives **all zeros**, and it
looks for all the world like the class has no virtual functions. It does not.
libmvos puts its vtables in **`.rodata`** and populates them with relocations in
**`.rel.rodata`** (`0x3f98` bytes of them). The stored values are 0 because the
`R_386_32` entries resolve against a symbol, not against an inline addend.

Two ways this bites:

- **Read the file and you conclude the slot is NULL.** `cConsole::Input`
  dispatches through `vt+0xc`; the raw bytes there are `00 00 00 00`, which reads
  as "this call crashes". With the relocation applied it is
  `Process__8cConsolePCc`, and the class works fine. Scan *every* `.rel*`
  section — this object has no `.rel.dyn`, so a scan that assumes that name finds
  nothing and silently confirms the wrong answer.
- **Ghidra shows no xrefs to virtual methods.** `cConsoleVO::Key` reports only
  `Entry Point [EXTERNAL]` and no callers, because the vtable slot that reaches it
  is one of these unresolved words. "No xrefs" here means "dispatched virtually",
  not "dead code" — the same shape as the `get_function_by_address` artifact in §3.

Layout, once resolved: GNU v2 vtables are **8-byte entries**, `{short delta;
short index; void *pfn}`, so entry *i*'s function pointer is at `vt + i*8 + 4`.
Entry 0 is the `__tf…` type-info function; the first real virtual is at `vt+0xc`.
Walking past the last real slot runs into the adjacent `__ti…` type-info nodes
and the mangled type-name string — which is the marker that you have reached the
end, not a slot full of garbage.

The same caution applies to `theocracy.real`, which resolves its vtables through
`.rel.got`/`.rel.bss`; the host's `guestlink` applies all of this at load, so the
*running* image is correct and only static file reads are exposed.

**Use `tools/elfq.py vtable <addr>` rather than re-deriving this.** It scans every
`.rel*` section, resolves each word to its symbol, and prints raw offsets instead
of assuming an entry stride — g++ 2.x emits either 4-byte pointer arrays or
8-byte `{delta, index, pfn}` structs depending on `-fvtable-thunks`, and these two
binaries do not agree, so a hardcoded stride reads a delta as a function pointer.

---

## 11. If it runs, run it before you read it

The dev-console work (2026-07-27) is the cautionary case. The feature was gated
by one branch, and a large static pass went into proving *which* branch and what
else read the same flag — analysis that was correct, already known, and not the
blocker. Two runs then produced both real causes in seconds:

- `[trap] TODO vsprintf` in the log — an **unimplemented host trap**, and the
  first call in `cConsole::Input`. No amount of patching the guest could have
  worked around it, and nothing in either binary hints at it, because the defect
  was ours.
- `SinglePalette font [data/fonts/small_red.mft]` — dissolved a "the console
  turns red, something is wrong" theory that had already been written into a doc
  as an open defect. It is just the console's font.

Neither is discoverable by reading the binaries; both were in output we already
had. The follow-on lesson is about *direction*: the question that cracked it was
not "why is the input gated?" but **"where does the output go?"** — which led in
three lookups to `Print(shell->+0x44, …)` and a console nothing ever shows.

So, when the port can execute the path: run it with the flag on, read the log,
and let the log choose what gets reverse-engineered. Reach for Ghidra when the
log names something you cannot resolve — not before. This sits alongside §7: the
running system is evidence, and it is usually cheaper evidence than the file.

## 12. A name is not a finding — and it propagates

The costliest error found so far cost nothing to make. `g_World+0x83c` was
described as the simulation's **order/command queue**, and
`FUN_081a2060`/`FUN_081a1fa0`/`FUN_081a2180` as its API. All three are `cDate`
arithmetic; `+0x83c` is the game date. Nothing anywhere reads a command from it.

Why it stuck, and what to take from each part:

- **The guess was reasonable.** A per-tick call, on an object the sim owns, at
  the top of a deterministic step function, is what an order queue *would* look
  like. Plausibility is not evidence, and it is precisely when a guess fits that
  it stops getting checked.
- **It became load-bearing.** A whole "determinism & lockstep" argument was
  built on top of it, complete with a conclusion — *deterministic, replayable,
  lockstep-synchronizable* — that read as a finding. The RE fact underneath had
  never been read.
- **It spread.** Three docs cited it, and one of them
  (`dev-console.md`) used it as the *contrast* for something else: "that pipe is
  not the sim's order queue". A wrong fact acquires dependents that look like
  corroboration.
- **An audit missed it.** The 2026-07-26 findings audit re-checked every cited
  address against the binaries. `g_World+0x83c` is a perfectly real address, and
  `FUN_081a2060` is a perfectly real function — so an address-checking pass had
  nothing to catch. **Checking that an address exists is not checking that the
  claim about it is true.**

It was found only when a *different* doc (`calendar.md`) read the same address
properly and the two descriptions collided. That is worth generalising: when
work lands in a doc, grep the other docs for the addresses it touches.

So: when a doc names something the code does not — `FUN_…` renamed to
`OrderQueue_Read`, or prose calling an address "the command queue" — either the
function has been read or the name is a hypothesis. Mark it as one. This project
has a house style for that (`inferred`, `TBD`, `strong candidate`); the failure
here was not that the guess was made but that it was written in the indicative.

---

## Checklist

Before a finding lands in `docs/` or in host code:

1. Which binary, and is the address in that binary's Ghidra space or its file
   space? Reconcile against the export table rather than retyping.
2. Does the cited entry point have real callers?
3. If the offset came off a `TYPE *`, has it been scaled?
4. Could this function be truncated by a bogus noreturn flag? For anything
   load-bearing, confirm against the instruction stream.
5. Is this a layout you read, or a layout you inferred from call sites?
6. Was the claim measured on the running system, or read off a file — and did
   any output get truncated on the way?
7. If you read a vtable or a pointer table out of a file and got zeros, did you
   apply the relocations — from *every* `.rel*` section, not just `.rel.dyn`?
8. If you hand-rolled a byte scan, does it decode the *whole* opcode group? An
   `80 /n` scan that only knows `/7` (cmp) misses `/6` (xor), so a
   read-modify-write toggle reads as "never written" — which is exactly how the
   cheat flags were first documented wrong. Prefer `tools/elfq.py`, which was
   built after that scan produced two wrong claims in committed docs. **And the
   same defect recurred in `elfq.py` itself** (2026-08-08): `xref-global` decoded
   only `A1`/`8B` pointer loads plus a byte-op table, so every dword `cmp`, every
   dword *write* and every `push`-the-address was invisible. It reported **zero
   references** to `MAX_FUCKER` (`0x84c8599`), a global read twice by `3B /r`
   ([../subsystems/population-and-births.md](../subsystems/population-and-births.md)).
   The fix inverts the search: find the 4-byte operand anywhere in `.text` and
   classify each occurrence *backwards* from it, reporting anything it cannot
   decode as `operand?` rather than dropping it. **Search for the operand, not
   for a list of opcodes** — an opcode table you enumerate is a table you can be
   wrong about silently; an operand match you classify afterwards is not.
9. Is the *name* in this claim something the function was read to do, or
   something it plausibly does? If the latter, is it written as a hypothesis —
   and does any conclusion elsewhere depend on it? (§12)
10. Do the other docs already say something about these addresses? `grep` them
    before committing; two docs disagreeing is the cheapest bug detector this
    project has, and it only works if someone looks.
