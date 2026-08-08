# Music: the CD-audio (Redbook) subsystem

**Theocracy's music is Redbook CD audio — the analogue tracks on the game disc,
played by the drive, never decoded by the game.** There is no music file
anywhere in the installed data tree, no MIDI, no module format, no streamed
codec. The engine asks the CD-ROM drive to play track *n* and that is the whole
mechanism.

This is why the port has never had music, and why nothing ever reported it as
missing: the guest asks for a track, the request reaches a stubbed driver table,
and the stub returns 0 without logging. It is not an unimplemented trap and it
never appeared in the `[vtable] TODO` census.

Addresses are `theocracy.real` (base `0x08048000`) unless stated. libmvos
addresses are Ghidra-space (base `0x10000`).

## The two audio paths are unrelated

| | Sound effects & ambience | Music |
|---|---|---|
| Source | `.wav` under `data/game/data/sounds/` (~16 MB) | CD audio tracks |
| Engine class | `cSoundCard_Linux` + software mixer | `cVCD` |
| Device | `/dev/dsp` (OSS) | `/dev/cdrom` (ioctl) |
| Port status | **works** — guest mixer green-run onto SDL | **stubbed to a no-op** |

`data/game/data/sounds/ambient/` is worth naming explicitly because it is the
thing most easily mistaken for music: `erdo.wav`, `mocsar2.wav`, `opart.wav` —
Hungarian for *forest* and *swamp*. Environmental loops through the ordinary
sample mixer, not score.

## `cVCDThread` — the music manager

One object, `0x94` bytes, `new`'d in `cApplication::Start` at `0x8144dbb` and
stored at the global **`0x084c9764`**. Constructor `0x081a35a0`.

It **is-a `cThread`** with `cThread` as the *primary* base at offset 0 — worth
stating because `cSoundCard_Linux` puts `cThread` at `+4` as a secondary base
(see [platform-audio-threads.md](platform-audio-threads.md)), so the two are not
read the same way. Running flag `+0x10`, vtable `+0x04`, as usual.

| Offset | Field |
|---|---|
| `+0x00` | `cThread` base (running flag `+0x10`, `pthread_t` `+0x14`) |
| `+0x18` | current mood (0–3, or 4 = stopped) |
| `+0x1c` | `cList` mood 0 |
| `+0x34` | `cList` mood 1 |
| `+0x4c` | `cList` mood 2 |
| `+0x64` | `cList` mood 3 |
| `+0x7c` | `cRandom` (seed at `+0x80`) |
| `+0x84` | `cSemaphore` |
| `+0x90` | enabled flag (`1` at construction) |

Each `cList` is `0x18` bytes = two `0xc`-byte `cNode`s, the Exec header idiom
already documented in
[memory-and-containers.md](memory-and-containers.md). Track nodes are `0x10`
bytes: `{succ, pred, vtbl, track byte at +0xc}`.

**Four moods exactly.** The constructor initialises four lists, and the
destructor (`0x081a38e0`) confirms it twice over — its node-free loop runs
`local_5 < 4`, and its header-unlink loop walks `this+0x7c` down to `this+0x1c`
in `0x18` steps, which is `(0x7c-0x1c)/0x18 = 4`. Two independent readings of
the same number, which is the discipline
[re-methodology.md](../reference/re-methodology.md) asks for.

## The track table

Six `AddTail` calls in the constructor. The insert idiom writes
`node->succ = L+0xc` and updates the pred slot at `L+0x10`, so each list base is
`target − 0xc`:

| Mood | List | Tracks | Context |
|---|---|---|---|
| **0** | `+0x1c` | **3** | menu / attract mode |
| **1** | `+0x34` | **8** | realm screen |
| **2** | `+0x4c` | **6, 7** | battle (one branch), tutorial/briefing |
| **3** | `+0x64` | **2, 5** | battle (other branch) |
| **4** | — | — | stop; not a list index |

**The disc therefore carries audio tracks 2–8: track 1 is the data track, seven
audio tracks follow, and the game names six of them.**

**Track 4 is never referenced.** `Play__4cVCDUl` has exactly one call site in the
entire binary (`0x081a3c67`, inside `cVCDThread_StartTrackForMood`), and
`cVCD::PlayAll` / `cVCD::GetNumberOfTracks` are not imported by the game at all.
So track 4 is not reachable through any code path — it is either a bonus track,
or the credits/end-title music triggered some other way, or it does not exist and
the disc has six audio tracks with a gap in the numbering the game assumes.
**Only the TOC of a real disc can tell these apart**, which is the single most
useful thing a rip will settle.

### The "random" choice is deterministic

The `cRandom` at `+0x7c` is seeded with `0x2a` and then immediately re-seeded
with the constant `0x07b9f846`. Neither is time- or entropy-derived, so for
moods 2 and 3 the sequence of track choices is **identical on every run of the
game**. Anyone comparing a port session against original hardware by ear can
rely on that: same mood transitions must produce the same tracks in the same
order, and a divergence is a real difference rather than luck.

## How a mood becomes a track

`cVCDThread::SetMood(mood)` — `0x081a3a70` — is the entire public API:

```c
if (this->mood == mood) return;          // idempotent; see below
Lock(this+0x84);
this->mood = mood;
if (this->enabled)
    mood == 4 ? VCD->driver[0x1c](VCD)   // stop
              : StartTrackForMood(this);
Unlock(this+0x84);
```

The early-out matters for reading the rest of the engine: `RealmGameLoop` calls
`SetMood(g_VCDThread, 1)` **every frame**, and after the first frame it does
nothing. This is the call that
[game-loop-and-simulation.md](game-loop-and-simulation.md) step 9 corrected from
"advance animations / frame sync" on 2026-08-03. It is now identified
completely: it is the music mood setter, and it is one of the reasons nothing in
the realm loop is frame-tied.

`StartTrackForMood` (`0x081a3b80`):

1. `cur = VCD->driver[0x20](VCD)` — the currently playing track.
2. Walk `cList[mood]`; **if any node's track == cur, return.** Changing to a mood
   whose music is already playing does not restart it.
3. `VM_GetCDRomName() == 0` → return. **Music requires the disc to be mounted**,
   through the same check that `original-os-setup.md` describes as gating the
   disc prompt.
4. Count the list, `Rnd(cRandom) * count` → index, walk to it.
5. `cVCD::Play(VCD, node->track)`.

The list walk is `n = n->succ; if (!n->succ) n = NULL` — Exec's tail-node
convention, not a NULL-terminated singly-linked list. Reading it as the latter
gives an off-by-one on the last element.

## The polling thread is what makes music continue

`cVCDThread::Main` (`0x081a39e0`), launched by `Launch__7cThread(this)` at the
end of the constructor:

```c
forever {
    50 times { if (!running) return; Sleep__11cSyncSystemUl(100000); }
    if (enabled && !DAT_084c9762 /* global mute */ && mood != 4) {
        Lock(sem);
        if (VCD->driver[0x20](VCD) == 0)   // nothing playing => track ended
            StartTrackForMood(this);
        Unlock(sem);
    }
}
```

**This is the only thing that advances music.** `SetMood` fires once per context
change; without this thread a track plays through once and is followed by
silence until the next screen transition. Any implementation that provides
`Play` but not the equivalent of this poll will sound correct for three minutes
and then go quiet — a failure mode that would be easy to misdiagnose as a decode
bug.

## The driver table at `VCD+0xc`

`cVCD` dispatches through a function table at `+0xc`, distinct from its C++
vtable. Slots observed from the game side:

| Slot | Use |
|---|---|
| `0x18` | called by `cVCDThread_UnmuteAndApplyVolume` when `DAT_08648380 > 0` — volume, most likely |
| `0x1c` | stop |
| `0x20` | get currently playing track (`0` = nothing) |

`0x1c` and `0x20` are certain from context. `0x18` is inferred from its guard and
has **not** been read in libmvos — see Open threads.

## What the port does today

`port/src/mvos.cpp:153-157` points `VCD+0xc` at `make_vtable(16)` so the CD
worker's `(*(X+0x1c))(VCD)` lands in `dispatch_vtable` and returns 0. That was
enough to stop a null-pointer fault during boot and nothing more was needed,
because the failure is silent all the way down:

- slot `0x20` returns 0, so step 2's "already playing?" check never matches;
- `VM_GetCDRomName()` **succeeds** — `resolve_path` maps `/mnt/cdrom` to
  `data/cd`, which has the real `cd.key` reading `Theocracy`;
- so `cVCD::Play` is reached, runs as real guest code, tries its `/dev/cdrom`
  ioctl, and fails against a host that has no such device.

So the game is *already asking for music* at every mood change, and has been
since the port first booted. Everything upstream of the device works.

## Open threads

- **What is on track 4, and how many audio tracks the disc really has.** The
  table names 2, 3, 5, 6, 7, 8 and skips 4. Answered by the TOC of a physical
  disc; see `todo.md`.
- **Moods 2 vs 3.** Both are set from the battle screen (`FUN_080bb590` @
  `0x80bbe79`), chosen by `vcall[+0xb4] || flag at world+0x40a4c` — mood 2 if
  either is true, mood 3 otherwise. The flag getter is `0x081ecaa0`. Neither
  predicate has been identified, so "battle" vs "the other battle" is as far as
  this goes. Mood 2 is also set by the tutorial/briefing screen (`0x8226280`,
  `0x82263a1`) and by `FUN_0820e7f0`, which is an unidentified screen loop.
- **`cVCD` itself is unread.** `Play` (`0x805b0`), `Play(start,end)`
  (`0x80580`), `PlayAll` (`0x805f0`), `GetNumberOfTracks` (`0x80640`), ctor
  (`0x80680`) — all in **libmvos**, none decompiled. Needed before writing a
  native override: the exact `/dev/cdrom` ioctls, whether `Play` is asynchronous
  (the polling thread implies it is), what track numbering base it uses, and
  what slot `0x18` of the driver table does.
- **The host has a second soft thread it never runs.** `traps.cpp`'s
  `pthread_create` pushes every `cThread` onto `soft_threads_`, and
  `maybe_redirect_sound` green-runs the *first* entry whose running flag at
  `arg+0x10` is set. Today that is always the `cSoundCard_Linux` mixer — but only
  because `OpenSubsystems` constructs the sound card before `cApplication::Start`
  constructs `cVCDThread`. The ordering is load-bearing and undeclared. Reviving
  music means running this second thread too, and its body is an infinite loop,
  so it cannot be green-run the way the mixer's one-shot-patched `Main` is.
  *(Predicted from the source, not yet observed in a run — a `THEOC_*` log line
  counting soft threads would confirm it.)*
- **The host mixer is single-stream.** `TrapLayer::audio_push`
  (`port/src/traps.cpp:646`) appends to one `audio_q_` deque that the SDL
  callback drains. It has only ever had one producer — the guest mixer's
  `/dev/dsp` writes, with cutscene audio playing when nothing else is. Music is
  the first case of two genuinely concurrent sources and needs a real mix (sum,
  clamp, independent rates), not an append. This is the substantive engineering
  in reviving music; getting the audio files is the easy half.
- **Decode strategy.** `mpeg.cpp` decodes whole movies into RAM up front, which
  is fine for a 1192-frame intro and wrong for a soundtrack: one five-minute
  track is ~26 MB at 22050 Hz stereo S16. Music wants streaming decode. libav is
  already a dependency, so FLAC rips cost nothing to read.
- **There is no reference recording.** The Woody VM in
  [original-os-setup.md](../reference/original-os-setup.md) was always run with a
  minimal boot ISO rather than the real disc, so the original's music has never
  been heard on original code. Until a disc is in a drive, nothing in this
  project has ever verified that any of the above produces sound.

## Related

- [platform-audio-threads.md](platform-audio-threads.md) — the *other* audio
  path: `cSoundCard_Linux`, OSS, the software mixer, `cThread`.
- [application-bootstrap.md](application-bootstrap.md) — the `Redbook`
  requirement flag and where `VCD` is created in `OpenSubsystems`.
- [game-loop-and-simulation.md](game-loop-and-simulation.md) — step 9 of
  `RealmGameLoop`, now fully identified as `SetMood(1)`.
