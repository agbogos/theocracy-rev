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

## 13. Absence in a shipped data file is not absence — most of them are encrypted

Chasing where hero stats come from, a `grep -i hero data/game/data/selap.txt`
returned nothing, and the conclusion drawn from it — "the hero constants are not
in `selap.txt`, so they must come from somewhere else" — was wrong and sent the
search off after another config file that does not exist. `selap.txt` ships
`RSA4096`-encrypted and contains **103** hero keys. A `wc -l` on the same file
was equally meaningless: 126 lines of ciphertext against 986 real ones.

This is the cheapest possible mistake to make, because the failure is silent and
looks exactly like a genuine negative result. The rule:

- **Never grep a file under `data/game/` directly.** Decrypt into memory first —
  `tools/theocracy_crypt.py` as a module, or the four-line XOR — and grep that.
  The tree must stay as-shipped on disk ([phls-format.md](phls-format.md)), so
  there is no decrypted copy to search by accident.
- **A negative result from a data file needs the same evidence as a positive
  one.** State how the file was read, not just what was not in it.
- The inverse is also worth knowing: not everything is encrypted. `hero.cfg` is
  plaintext, `.raw` assets are plaintext, and the `.sdb` locale files use a
  *different* cipher again. "Is this file encrypted, and with which of the
  three?" is a question to answer before reading, not after being confused.

---

## 14. A differing vtable slot is not behaviour

Classifying the 50 magic items, the obvious test for "does this item do
anything?" was whether its vtable differs from the shared default. Every one of
the 50 differed in at least one slot, giving the clean and wrong answer **"no
item is unimplemented"**. Reading the bodies showed that seven of the 90
overrides do nothing but call the slot's own default and normalise the return
value — `return default(this, m) != 0`, fifteen instructions of nothing. Three
items behave exactly as the base class and the structural test called all three
implemented.

The generalisation, which applies well beyond vtables: **a difference in a table
is evidence that a compiler emitted something, not that a programmer meant
something.** g++ 2.95 emits a distinct thunk per class for covariant returns,
access adjustments and inlined trivia, so "has its own entry" is nearly free.

- Compare *bodies*, not addresses, whenever the conclusion is about behaviour.
- A useful shape for the test: does the override call its own default, and does
  it add anything around that call? Length alone is a poor proxy — the first
  attempt here used "≤ 12 instructions" and missed a 15-instruction no-op.
- The same caution applies to any per-item or per-class table: distinct entry ≠
  distinct behaviour. See §12 — this is the structural cousin of naming a
  function you have not read.

---

## 15. A factory tells you who uses the factory, not who builds the type

`magic-items.md` recorded `Item_CreateById` (`0x0820d1f0`) as "the only way an
item comes into existence" — a switch over ids 1..50, each case calling that
item's own constructor. It reads like a chokepoint, so the mission task's
cheapest first move was to xref it. Eight call sites came back: two config-file
placers, save-load, and the developer console. **No mission.**

Every one of those eight is real, and the conclusion they invite —
"missions don't place items, so the Ring Pieces are unreachable" — is false.
Mission code does `new(0x18)` and calls `cMagicItem_RingPiece1_ctor` **directly**,
skipping the switch. Xref'ing the fifty constructors instead of the one factory
turns up 25 with a non-factory caller, and with them the entire quest-reward
layer: the ring combination, five hero rewards, and 25 item placements.

- **A factory is a convenience for its callers, not a gate on construction.** In
  C++ it cannot be one: any code with the class definition can call the
  constructor. Treat "the only way X is created" as a claim needing the
  *constructors* xref'd, and say which you checked.
- The tell was available and was not read: `Item_CreateById`'s callers are all
  **data-driven** — a config file, a save, a console string. Code that knows at
  compile time which item it wants has no reason to go through an id switch.
- Cost here: near zero, because the false negative was caught within one step.
  It would have been expensive one step later — "the Ring Pieces combine
  nowhere" was ready to be written into two docs.

### …and the same mistake one level down, in the same session

The write-up above claimed, as its own contrast case, that `cHero_SetHeroId`
*is* the sole writer of the hero id — "established by scanning writes to the
field (`+0x27c`), not by trusting the setter's name." **That was wrong**, and
wrong for a third distinct reason, caught within the hour by a reader who knew
the game's story: Jarakhi is the campaign's player character and is obviously in
the game, while the audit said no code places him.

Two defects, both in the *scan*, not in the reasoning built on it:

1. **A `lea` is a write you cannot see.** The scan matched stores — `movb %cl,
   0x27c(%ebx)`. `cHero`'s stream constructor instead does `leal 0x27c(%ebx),
   %edx` and hands that address to a stream reader, which fills the byte from
   file data. Taking the address of a field is a write in every sense that
   matters, and it matches no store pattern. **Grep for `lea` on the field too,
   or the "sole writer" claim is only about literal `mov`s.**
2. **Construction through a registered function pointer has no call site.** The
   stream constructor is reached through the per-man-type *caste properties*
   struct, which stores three function pointers (`FUN_08254570` writes
   `_DAT_0866d828 = FUN_082543b0`). There is no rel32 to find. The `.eh_frame`
   check that cleared the item constructors of exactly this suspicion was never
   run on the man side.

And the deeper one, which is not about scanning at all: **a code audit cannot
see content that ships as data.** Every `init.dat` in the tree is a `theosg42`
savegame — the starting world of the campaign and of all eight scenarios is
*loaded*, not placed. `save-format.md` already recorded that the console's
`save` command writes `init.dat`; the fact was in the repo and simply was not
connected.

- Before writing "X is created/assigned by nothing", enumerate the **channels**,
  not just the call sites: code, config files, *and serialised state*. This
  project ships its initial world as save data, so the third channel is always
  live for anything an object can carry.
- The tell was available: `hero.cfg` is loaded only under
  `*(int *)(g_GameSession + 0x4c) == 0`. A condition on *placement* implies a
  path where placement is skipped because the objects already exist.
- Domain knowledge outranks a clean sweep. The sweep was internally consistent,
  exhaustive over what it searched, and produced "the player character does not
  exist". **A result that contradicts something a player would know is a defect
  in the search, not a discovery.**

## 16. To read a deep format, instrument the reader you already have

The follow-on task from §15 was "parse `init.dat` and say what is in it". The
obvious shape is a parser: walk the stream constructors, mirror them in Python,
declare victory when the file is consumed exactly. That is a real and verifiable
plan, and it was started — the load chain in
[../subsystems/starting-world.md](../subsystems/starting-world.md) is its
output. It was abandoned three hours in, correctly.

The reason is the shape of the cost curve. A savegame parser is **all-or-nothing**:
it produces no partial answer, because a single wrong field size desynchronises
everything after it, and the chain here is ~150 stream constructors deep —
provinces, 58 building classes, 42 man castes, roads, towns, unit commands,
message queues. Every one of them has to be right before the first hero id can
be read. Meanwhile the game contains a parser that is right by construction, and
the port already runs it.

So the answer came from four passive `UC_HOOK_CODE` watches — `LoadGame`, the
caste read in `CreateMan_fromStream`, the hero-id read, and `Item_CreateById` —
and nine headless runs. About eighty lines of host code against a multi-day
re-implementation, and the result is stronger: it is what the shipped loader
actually does, not what a reading of it predicts.

- **Ask what the artefact is for.** A format that only one program reads has
  exactly one authority, and it is not your parser.
- **Prefer the chokepoint the design already has** to the one you would build.
  `Item_CreateById` is a 50-way switch every item passes through, so hooking it
  cannot miss a subclass; hooking `Item_CreateFromStream` would have been
  narrower and would have needed a correctness argument.
- **Instrument a control alongside the signal.** The first run reported zero
  heroes and zero items, which is indistinguishable from "the watches never
  fired" — and on that run it was in fact a bad run. The caste watch was added
  precisely so that "1248 men" states, in the same line, that the instrument is
  alive. Any watch-based finding needs one quantity that cannot legitimately be
  zero.
- The limit is honest and worth stating: this answers *what the loader does with
  this file*, not *what the bytes are*. For "which heroes ship in the campaign"
  those are the same question. For "what is the on-disk layout of a `cMan`" they
  are not, and then the parser is the only route.

This is §11 (*if it runs, run it before you read it*) applied to a data format
rather than to a bug: the running system is evidence, and it is usually the
cheaper evidence.

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
11. If a claim rests on a data file — especially a *negative* claim — was the
    file decrypted before it was read? (§13)
12. If a claim rests on a table differing per entry (vtables, dispatch tables),
    were the *bodies* compared, or only the addresses? (§14)
