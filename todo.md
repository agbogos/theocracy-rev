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

### ~~2. Rip the CD audio tracks~~ — done 2026-08-08

UK release ripped to `data/cd-uk/` as AIFF-C, tracks 2–8, TOC verified against
the prediction. Kept below for the *other* discs, which are still worth a TOC
read: the Chinese `.mds` costs nothing and would say whether the track numbering
is release-invariant (i.e. whether the port can ship one mapping for any disc a
tester owns). Not blocking anything.

<details><summary>original task</summary>

### Rip the CD audio tracks (music)

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

Tracks are copyrighted — they go in `data/cd-uk/` (gitignored), never in git.

</details>

---

## CLAUDE

Nothing queued. The music subsystem closed out on 2026-08-08 — what is left of
it is genuine open threads in its own doc rather than tasks
([`music-and-redbook.md`](docs/subsystems/music-and-redbook.md)), and the one
*direction* it produced is the named first candidate in
[`native-rewrite.md`](docs/porting/native-rewrite.md).
