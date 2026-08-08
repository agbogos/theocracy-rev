# Native rewrite — retiring Unicorn, gradually

**Status: not started. This is the long game.**

The goal is to replace the emulated engine with native C++ **one piece at a
time**, with the game playable at every step, until Unicorn has nothing left to
execute and can be removed.

## Why this is now the direction

The port is finished as a port: single-player and multiplayer run end to end,
and the modernisation list closed on 2026-08-03. Everything still wanted from
here — a sim that is not welded to the frame rate, higher-resolution art, any
change to game logic — needs the engine to be *ours*, not emulated. The
frame-tied province simulation is the clearest case: it is unfixable from
outside (see [frame-timing.md](frame-timing.md), "Why province stays at 12fps")
and trivial once the code is native.

## Why this is not the 2026-07 plan again

The **pure-HLE native-replace** attempt is superseded, and this is not a revival
of it. That plan tried to reimplement the libmvos API boundary *before* anything
ran, and hit an unbounded wall: rendering a menu meant hand-writing the entire
GUI toolkit. See [macos-hle-emulator.md](macos-hle-emulator.md).

The difference is direction and evidence. We now have a **running system** to
replace pieces of, so every step is verifiable against the emulated original
rather than against a guess, and any piece can be reverted the moment it
misbehaves.

## The seam already exists

This is not new machinery — it is the mechanism `blit.cpp` has used since the
province-performance work. `Machine::add_code_traps` aimed at a single byte
inside the mapped libmvos image replaces one function with a native
implementation; the real body never runs. Five LFB16 rasterizer functions are
already native, with `THEOC_NATIVE_BLIT=0` to fall back for A/B.

`cGD_LFB16::Refresh` (2026-08-03) is the same seam used the other way — to
*implement* a method whose emulated body was a no-op that no longer suited us.

So the work is: keep widening that set, in dependency order, until the emulated
side is empty. See [host-architecture.md](host-architecture.md), "Native
override of a real libmvos function".

## What makes a good next candidate

Roughly in the order these matter:

1. **Leaf-first.** Something that calls little or nothing else, so the native
   version cannot drag in half the engine.
2. **Already understood.** Prefer functions a `docs/` page already describes;
   guessed struct layouts are this port's dominant bug class
   ([re-methodology.md](../reference/re-methodology.md)).
3. **Verifiable.** A function whose output can be compared against the emulated
   original — pixels, a return value, a memory range — rather than judged by eye.
4. **Worth it.** Either hot, or standing between us and something we want to
   change.

### A named candidate: a music volume control

The one concrete "fix something the original got wrong" item on the list, added
2026-08-08 after music was revived. The game ships **SFX and ambience sliders
that both work, an on/off toggle for CD music, and no music volume control at
all** ([music-and-redbook.md](../subsystems/music-and-redbook.md)). CD audio is
on or off, nothing in between — a gap in the original, not something the port
lost.

It scores unusually well against the four tests above: the whole music subsystem
is decompiled and documented, `cCD_Linux::SetVolume` already exists and is
already implemented host-side (`CDROMVOLCTRL` scales the mix), and
`THEOC_MUSIC_VOL` proves the plumbing end to end today. What is missing is
**only the widget** — which is also the honest warning, because the GUI toolkit
is named below as one of the hard parts, and "add one slider to an existing
options screen" is precisely the kind of task that turns out to require
understanding `cVObject` layout properly. Good first *real* test of whether the
GUI is approachable, on a change small enough that failing costs little.

## The hard parts, stated up front

- **Shared state.** Native code and emulated code operate on the *same* guest
  memory, so every replaced function must keep its struct layout byte-exact.
  This is what the `docs/structs/` pages are for.
- **The GUI toolkit.** The thing that killed the pure-HLE plan is still the
  largest single mass of work, and it is nearly all of `cVObject` and its
  subclasses.
- **The game binary, eventually.** `theocracy.real` is `.symtab`-stripped and
  its simulation is still largely a black box
  ([simulation-step.md](../subsystems/simulation-step.md), "Open threads"). Retiring Unicorn means
  understanding it, not just the engine.
- **Knowing when to stop.** A port that is 90% native and permanently mid-flight
  is worse than either endpoint. There is no deadline here, but there should be
  an honest checkpoint.
