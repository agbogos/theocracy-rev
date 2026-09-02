# Native rewrite — why this repo does not do it

Status: out of scope, and staying that way. This doc says why, and leaves what
is known to anyone who picks the idea up.

The idea was to replace the emulated engine with native C++ one function at a
time, with the game playable at every step, until Unicorn had nothing left to
execute and could be removed.

## Why not

The port is finished as a port. Single-player and multiplayer run end to end on
macOS, Linux and Windows, and the modernisation list — frame pacing, music,
filtering, fullscreen — is closed.

What going further would buy is a short list, and every item on it is a change
to the game rather than to the host: a simulation not welded to the frame rate,
higher-resolution art, altered game logic. This project runs the binaries Philos
shipped and writes down how they work. A native rewrite would instead reproduce
them, which is a different undertaking with a different relationship to someone
else's copyrighted work, and the author does not intend to pursue it.

## What that costs

Consequences of the decision, recorded so they are not re-opened as defects:

- Province view stays at 12fps. It is frame-tied inside the engine and
  unfixable from outside — [frame-timing.md](frame-timing.md), "Why province
  stays at 12fps", has the evidence. `THEOC_PROVINCE_MS` is the one pacing
  control the engine admits.
- The art stays at its shipped resolution. There is no geometry to re-render and
  no higher-resolution source; [upscale-filtering.md](upscale-filtering.md)
  covers what filtering can and cannot recover.
- Game logic stays as it is, including what the original got wrong. The clearest
  example is the missing music volume control, below.

## The seam exists anyway

Not as a foothold — compatibility work needed it. `Machine::add_code_traps`
aimed at a single byte inside the mapped libmvos image replaces one function
with a native implementation, and the real body never runs. `blit.cpp` has used
it since the province-performance work: five LFB16 rasterizer functions are
native today, with `THEOC_NATIVE_BLIT=0` to fall back for A/B.
`cGD_LFB16::Refresh` is the same seam used the other way, to implement a method
whose emulated body was a no-op that no longer suited the host.

So the mechanism a native rewrite would be built on is already in the tree and
already exercised. See [host-architecture.md](host-architecture.md), "Native
override of a real libmvos function".

## For anyone who does pursue it

What a good candidate looks like, in the order these matter:

1. Leaf-first. Something that calls little or nothing else, so the native
   version cannot drag in half the engine.
2. Already understood. Prefer functions a `docs/` page describes; guessed struct
   layouts are this port's dominant bug class
   ([re-methodology.md](../reference/re-methodology.md)).
3. Verifiable. Output that can be compared against the emulated original —
   pixels, a return value, a memory range — rather than judged by eye.
4. Worth it. Either hot, or standing between you and something you want to
   change.

The smallest concrete instance is a music volume control. The game ships working
SFX and ambience sliders, an on/off toggle for CD music, and no volume control
for the music at all
([music-and-redbook.md](../subsystems/music-and-redbook.md)) — a gap in the
original, not something the port lost. It scores well on all four tests: the
music subsystem is decompiled and documented, `cCD_Linux::SetVolume` exists and
is implemented host-side (`CDROMVOLCTRL` scales the mix), and `THEOC_MUSIC_VOL`
proves the plumbing end to end today. Only the widget is missing, which is also
the warning: adding one slider to an existing options screen means understanding
`cVObject` layout, and the GUI toolkit is the hard part below.

The hard parts:

- Shared state. Native and emulated code operate on the same guest memory, so a
  replaced function must keep its struct layout byte-exact. That is what
  `docs/structs/` is for.
- The GUI toolkit. What killed the pure-HLE plan
  ([macos-hle-emulator.md](macos-hle-emulator.md)) is still the largest single
  mass of work, and it is nearly all of `cVObject` and its subclasses.
- The game binary. `theocracy.real` is `.symtab`-stripped and its simulation is
  largely unread ([simulation-step.md](../subsystems/simulation-step.md), "Open
  threads"). Retiring Unicorn means understanding the game, not just the engine.
- A stopping point. A conversion left half-finished is worse than either end
  state, so decide in advance what done means.

## The licence rules it out separately

This repo's own code is `GPL-2.0-or-later`, both forced by Unicorn (see
[README.md](../README.md), "Licence"), and consciously chosen by the author.
The GPL requires the complete source of whatever is distributed to be released
under the same terms, and nobody can grant those terms over code they do not own.
A native reimplementation written by reading these decompiles would be derived
from Philos's work, so shipping it from here would mean licensing someone else's
expression under the GPL — which is not the author's to license.

The emulator has no such problem. It runs the shipped binaries without
containing them, and those binaries are not in this repository, so the only
thing distributed under the GPL is code we wrote. A clean-room
reimplementation — a specification written by one party and implemented by
another who has never seen the original — would avoid the derivation, but that
is not available here: `docs/` is written from the decompiles by the same
person who would write the replacement.
