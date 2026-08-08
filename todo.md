# todo

Open tasks only. A task leaves this file when it is done — the finding it
produced goes into `docs/`, which stays the record. Nothing here is a plan or a
rationale; if it needs either, it belongs in a doc that this file links to.

Two sections because the split matters: **YOU** is work that needs a machine,
hardware or a judgement only you have. **CLAUDE** is work I can do unattended.

---

## YOU

### 1. Windows on bare metal — blocked until ~2026-08-18

Hardware arrives via a third party around then. When it does, on that machine:

```
win-timing-probe.exe
win-timing-probe.exe --busy <core count>
win-timing-probe.exe --busy <core count x 2>
```

plus one ordinary game session with `THEOC_FPS=1`.

Two specific things only bare metal answers, both recorded in
[`docs/porting/other-os-ports.md`](docs/porting/other-os-ports.md), "The
contention runs": whether the ~98 ms loaded province frame is a VM artefact, and
what happens on a machine where nothing has already raised the global timer
resolution to 1 ms (the VM's was already raised before the probe ran).

### 2. Rip the CD audio tracks (music)

The score is Redbook CD audio; there is no music file in the data tree. The
game's track table is decompiled — [`music-and-redbook.md`](docs/subsystems/music-and-redbook.md).

**TOC first, on every disc you can reach** — this is the check, and it is
seconds per disc:

```
drutil toc                     # macOS, external drive
cdparanoia -Q                  # Linux
```

The prediction to test: **track 1 is data, audio tracks run 2–8**. The game
names 3 (menu), 8 (realm), 6 and 7 (battle), 2 and 5 (battle) — and **never
names track 4**. What is on track 4, and whether it exists at all, is the one
thing the binary cannot answer.

Then, from the British 2-CD release (both discs — which one carries the audio is
itself unknown):

```
cdparanoia -B                  # lossless, one file per track
flac *.wav                     # or keep WAV; libav reads either
```

Do **not** rip to MP3. Record the TOC alongside the audio.

Also worth doing because it needs no drive: parse the `.mds` sidecar of the
Chinese release for its TOC and compare track counts. If the two releases agree,
the numbering is release-invariant and the port can ship one mapping; if they
disagree, that is a finding and the port has to detect rather than assume.

Tracks are copyrighted — they go in `data/cd/` (already gitignored), never in
git.

---

## CLAUDE

### 1. Virtual CD device in the `ioctl` trap

The RE is done — the device contract is seven Linux CD ioctls and the driver is
stateless per call
([`music-and-redbook.md`](docs/subsystems/music-and-redbook.md)). Implement them
in `traps.cpp` so `cVCDThread`/`cVCD`/`cCD_Linux` all keep running as original
guest code:

- tag CD fds in the `open` handler (`traps.cpp:2814`) next to the existing
  `is_dsp` flag;
- replace the blanket `ioctl` stub (`traps.cpp:1674`) with a dispatch on the fd
  tag: `0x5305` READTOCHDR, `0x5304` PLAYTRKIND, `0x5307` STOP, `0x5301` PAUSE,
  `0x5302` RESUME, `0x530b` SUBCHNL, `0x530a` VOLCTRL — keeping the blanket
  success for every other fd, which is what `/dev/dsp` relies on;
- auto-advance: call `cVCDThread_StartTrackForMood` (`0x81a3b80`) from the host
  when the virtual CD goes idle.

**Depends on the rip** for real audio — but the TOC half (`READTOCHDR`,
`SUBCHNL`, and the mood→track plumbing) can be built and verified against
silence first, since the track table is already known. Worth doing that way:
it separates "is the guest asking for the right track" from "does the audio
decode", and only the first is subtle.

Also needs the host mixer to stop being single-stream — see the doc's Open
threads.

### 2. Confirm the second soft thread

Predicted from source, not observed: `cVCDThread` calls `pthread_create`, so
`soft_threads_` should hold **two** entries, and `maybe_redirect_sound` picks the
mixer only because it is constructed first. Add a log line counting soft threads
with their entry addresses, then read it off your next ordinary session — no
special run needed.

### 3. Rename one game-binary function (needs `.real` open in Ghidra)

`0x081a3c80` is currently `cVCDThread_UnmuteAndApplyVolume`, named before the
libmvos side was read. Driver slot `0x18` is **`Resume`**, not a volume call, so
the accurate name is closer to `cVCDThread_UnmuteAndResume`. Trivial, but it is
exactly the "plausible name becomes a fact" failure
[`re-methodology.md`](docs/reference/re-methodology.md) §12 documents. The calendar and the video-mode error string were both verified
against `theocracy.real` on 2026-08-05; what is left of each is a genuine open
thread in its own doc, not a task —
[`calendar.md`](docs/subsystems/calendar.md) and
[`original-os-setup.md`](docs/reference/original-os-setup.md).
