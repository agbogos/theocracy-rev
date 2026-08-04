# todo

Open tasks only. A task leaves this file when it is done — the finding it
produced goes into `docs/`, which stays the record. Nothing here is a plan or a
rationale; if it needs either, it belongs in a doc that this file links to.

Two sections because the split matters: **YOU** is work that needs a machine,
hardware or a judgement only you have. **CLAUDE** is work I can do unattended.

---

## YOU

### 1. Windows probe under contention — VM, ~5 min

The probe already ships in the bundle. In `dist/theoc-windows-x64/`:

```
win-timing-probe.exe --busy 12
win-timing-probe.exe --busy 24
```

Send me both outputs; I write them into
[`docs/porting/other-os-ports.md`](docs/porting/other-os-ports.md).

- Not `--busy 4` — four spinners on twelve logical CPUs measures the idle case
  while looking like a contention test.
- Acceptance criteria are already written down there, under "What closes the
  timing item", so the numbers can't be rationalised after the fact.

### 2. macOS sleep floor — one interactive run, no extra work

Next time you launch normally, add `THEOC_FPS=1` and send me one `[fps]` line.

The new sleep column reads `(N slices/frame, +M ms each)`. The only reading so
far is +2.1–3.2 ms/slice from a headless run that sat at 10.4 fps instead of 12,
which is not trustworthy — see
[`docs/porting/diagnostics.md`](docs/porting/diagnostics.md), "Reading the sleep
slices". One clean line replaces it.

### 3. Windows on bare metal — blocked until ~2026-08-18

Hardware arrives via a third party around then. When it does, on that machine:

```
win-timing-probe.exe
win-timing-probe.exe --busy <core count>
win-timing-probe.exe --busy <core count x 2>
```

plus one ordinary game session with `THEOC_FPS=1`.

This is the last thing standing between the Windows port and having no caveats
on any of its timing numbers.

### 4. Windows: watch one cutscene — VM, 1 min

The bundle's ffmpeg was replaced with a minimal build on 2026-08-04 (131 MB →
7.3 MB). Rebuild with `tools/package-windows.sh`, copy it over, and confirm the
intro plays with video **and** sound.

Why it needs a human: the failure mode is silent. The port logs `[smpeg] decode
failed, will skip frames` and carries straight on to the menu, so a broken build
looks like a working one unless someone watches. Identical config is verified
decoding on Linux amd64 and arm64, so this is confirmation, not a real risk.

---

## CLAUDE

Nothing queued.
