# Music: the CD-audio (Redbook) subsystem

**Theocracy's music is Redbook CD audio — the analogue tracks on the game disc,
played by the drive, never decoded by the game.** There is no music file
anywhere in the installed data tree, no MIDI, no module format, no streamed
codec. The engine asks the CD-ROM drive to play track *n* and that is the whole
mechanism.

This is why the port has never had music, and why nothing ever reported it as
missing. The whole chain — the game's music manager, `cVCD`, and the real
`cCD_Linux` driver — runs correctly as guest code all the way down to the
device, where the host's blanket `ioctl` stub tells it *success* and plays
nothing. Nothing fails, so nothing logs.

Addresses are `theocracy.real` (base `0x08048000`) unless stated. libmvos
addresses are Ghidra-space (base `0x10000`), so they are the **file** addresses
in `data/mvos_exports.tsv` **plus `0x10000`** — `cVCD::Play` is file `0x805b0`
and Ghidra `0x905b0`. This doc uses Ghidra addresses throughout, per
[re-methodology.md](../reference/re-methodology.md) §1.

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

## The engine side: `cVCD` is abstract, `cCD_Linux` is the driver

`cVCD` (libmvos) is a **shell**. Every operational slot of `__vt_4cVCD`
(`0xb7200`) points at `__pure_virtual` (`0xa6160`, which prints
`pure virtual method called` and `_exit(-1)`). The class holds state and
delegates through a driver table at `+0xc`.

The concrete driver is **`cCD_Linux`**, and `OpenSubsystems` builds the object
**inline** — no constructor is ever called, not `cVCD::cVCD` (`0x90680`) and not
`cCD_Linux::cCD_Linux` (`0xa1f80`):

```c
if (cApplication::Redbook) {
    obj = new(0x14);
    dev = GetCDRomDeviceName();            // 0xa4840
    obj[0xc]  = cCD_Linux_virtual_table;   // __vt_9cCD_Linux @ 0xbc240
    obj[0x10] = dev;
    VCD = obj;
}
```

`GetCDRomDeviceName` reads `mvos.cfg` `[vmachine] cdrom_device` and **defaults to
`/dev/cdrom`**. Our `data/game/mvos.cfg` does not set it, so `/dev/cdrom` it is.

### `cVCD` object layout (`0x14` bytes)

| Offset | Field |
|---|---|
| `+0x00` | `u16` start track |
| `+0x02` | `u16` end track |
| `+0x04` | `u16` start track (copy) |
| `+0x06` | `u16` **first** track on disc |
| `+0x08` | `u16` **last** track on disc |
| `+0x0c` | driver table |
| `+0x10` | `char*` device path |

`+0x06`/`+0x08` are **left uninitialised** by that inline construction and only
become meaningful after the first `GetCDInfo`. Note that
`cVCD::GetNumberOfTracks` returns `+0x08`, which is the *last track number*, not
a count — a distinction that matters the moment a disc's numbering has a gap.

### The driver table — the exact contract

`__vt_9cCD_Linux` @ `0xbc240`, dumped relocation-aware from the ELF:

| Slot | Function | ioctl |
|---|---|---|
| `+0x08` | `GetCDInfo()` | `0x5305` `CDROMREADTOCHDR` |
| `+0x0c` | `Play_Real(start, end)` | `0x5304` `CDROMPLAYTRKIND` |
| `+0x10` | `~cCD_Linux()` | — |
| `+0x14` | `Pause()` | `0x5301` `CDROMPAUSE` |
| `+0x18` | `Resume()` | `0x5302` `CDROMRESUME` |
| `+0x1c` | `Stop()` | `0x5307` `CDROMSTOP` |
| `+0x20` | `GetActualTrack()` | `0x530b` `CDROMSUBCHNL` |
| `+0x24` | `GetVolume()` | `0x530a` `CDROMVOLCTRL` |
| `+0x28` | `SetVolume(u16)` | `0x530a` `CDROMVOLCTRL` |

Plain Linux CD-ROM ioctls, nothing exotic. **Every method opens the device, does
one ioctl, and closes it** — the driver keeps no fd and no state between calls,
which makes it trivial to virtualise.

Details worth having before implementing:

- `Play_Real` packs `struct cdrom_ti` as `(start & 0xff) | (end << 16)`, i.e.
  `trk0=start, ind0=0, trk1=end, ind1=0`. It is **asynchronous** — the ioctl
  returns when the drive starts, which is exactly why the polling thread exists.
- `SetVolume(u16)` uses the **high byte** of its argument, replicated into all
  four channels of `struct cdrom_volctrl`.
- `GetCDInfo` fills `+0x06`/`+0x08` from `struct cdrom_tochdr`.

### `GetActualTrack` — read the disassembly, not the decompile

Ghidra renders `GetActualTrack` (`0xa1de0`) as a bare `return 0` with
*"Switch with 1 destination removed"* and *"Exceeded maximum restarts"*. That is
wrong, and it is the artifact class
[re-methodology.md](../reference/re-methodology.md) exists to catch. The real
body reads `struct cdrom_subchnl` (requested as `CDROM_MSF`, format 2) and
switches on `cdsc_audiostatus` through a 22-entry jump table at `0xbc1e0`:

| `cdsc_audiostatus` | Result |
|---|---|
| `0x11` `CDROM_AUDIO_PLAY` | return `cdsc_trk` |
| `0x12` `CDROM_AUDIO_PAUSED` | return `cdsc_trk` |
| all others (incl. `0x13` `COMPLETED`, `0x14` `ERROR`, `0x15` `NO_STATUS`) | return `0` |

Dumped from the file rather than assumed: only statuses `0x11` and `0x12` reach
the return-track path. Two consequences that any implementation must reproduce:
**paused still reports a track**, so pausing does not make the poll thread think
the track ended; and **completed reports 0**, which *is* the advance trigger.

## What the port does today

> **Correction (2026-08-08, same day).** An earlier revision of this doc said
> the host stubs `VCD+0xc` with a synthetic vtable from
> `port/src/mvos.cpp:153-157`. **That code is not in the build** —
> `port/CMakeLists.txt:138` has `src/mvos.cpp` commented out as the superseded
> pure-HLE layer, and nothing constructs `Mvos`. `VCD+0xc` is therefore the
> genuine `cCD_Linux_virtual_table` that `OpenSubsystems` installs, and the real
> driver runs. The conclusion (no music, silently) was right; the mechanism was
> not, and the mechanism is what an implementation has to hook.

Every layer runs as original guest code. The chain reaches the host at exactly
two traps, and both currently *succeed*:

1. **`open("/dev/cdrom", O_RDONLY)`** — `traps.cpp:2814` returns a synthetic fd
   for any path under `/dev/`, without touching the host filesystem. So
   `cCD_Linux`'s open **succeeds**.
2. **`ioctl(fd, ...)`** — `traps.cpp:1674` is a blanket `return 0` with a comment
   about `/dev/dsp` probes. On Linux `0` means **success**, and the stub never
   writes the output buffer.

Follow that through and the behaviour is precise:

| Call | What happens now |
|---|---|
| `GetCDInfo` | "succeeds"; `+0x06`/`+0x08` are filled from **uninitialised stack** |
| `Play_Real` | "succeeds" — **the game believes music is playing** |
| `GetActualTrack` | buffer is zeroed by the guest, stub writes nothing, so `cdsc_audiostatus = 0` → table entry 0 → returns `0` |
| `Stop` / `Pause` / `Resume` / `SetVolume` | "succeed", do nothing |

So the game has been asking for music at every mood change since the port first
booted, and being told it got it. The garbage in `+0x06`/`+0x08` is harmless
today only because `PlayAll` and `GetNumberOfTracks` are never called.

## The implementation this points at

**Implement the CD as a virtual device in the `ioctl` trap, not as native
overrides of `cVCD`.**

The seven ioctls above are the entire device contract, the driver is stateless
across calls, and `open` already hands out a taggable fd. Implementing them
against a host-side player backed by ripped audio files leaves `cVCDThread`,
`cVCD` and `cCD_Linux` running **completely unmodified as original guest code** —
no vtable patching, no native method overrides, nothing bypassed. A CD-ROM drive
is an OS device, so this lands exactly on the boundary
[guest-libmvos.md](../porting/guest-libmvos.md) says the port HLEs and no deeper.
It is a smaller and more faithful change than the native-override route this doc
originally assumed.

The one piece that does *not* fall out for free is the auto-advance, because
`cVCDThread::Main` never runs (below). Two options, in preference order:

1. **Call the guest directly.** When the host's virtual CD goes idle, use the
   existing host→guest call path to invoke
   `cVCDThread_StartTrackForMood(g_VCDThread)` at `0x81a3b80`. That does exactly
   what the poll would have done, picks the correct mood's track list, and needs
   no thread and no patching.
2. **Green-run the thread**, patching its `Main` to one-shot the way
   `patch_sound_main_oneshot` does for the mixer. More faithful, but the patch
   target is in `theocracy.real` rather than libmvos, and the loop shape is
   harder to cut than the mixer's.

A host-side loop of the current track would also produce continuous music, but
it is *not* faithful — the original picks a fresh random track from the mood list
each time — so it belongs in neither option.

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
- ~~`cVCD` itself is unread~~ — **done 2026-08-08**, and it turned out to be an
  abstract shell over `cCD_Linux`; the driver contract is the ioctl table above.
  What is *not* answered: `GetVolume` (`0xa1e70`) reads back through
  `CDROMVOLCTRL` but nothing in the game calls it, and the `DAT_08648380` volume
  setting that gates `Resume` in `cVCDThread_UnmuteAndApplyVolume` has not been
  traced to where the options screen writes it.
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
