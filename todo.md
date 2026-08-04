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

---

## CLAUDE

### 4. Bundle size reduction — not started

Both bundles are enormous for the same reason, in different shapes:

| Bundle | Size | Where it goes |
|---|---|---|
| Windows | ~131 MB | `avcodec-61.dll` 89.6 MB + `avformat-61.dll` 21.1 MB (codecs statically linked in) |
| Linux | ~190 MB | the same codecs as separate `.so` files — x264, x265, vpx, theora, srt, zmq — pulled in as `DT_NEEDED` of Debian's libavcodec |

`port/src/mpeg.cpp` decodes **MPEG-1 video + MP2 audio out of an MPEG-PS file**,
plus swscale for YUV→RGB565 and swresample for the audio rate. Everything else
in those libraries is dead weight.

Plan: one `tools/build-ffmpeg-min.sh` that builds a `--disable-everything`
ffmpeg with just those decoders/demuxers/parsers into a prefix, and point both
package scripts at it with the `-DTHEOC_PREFIX=` override that already exists.
Shared, not static, so the closure walk in both scripts keeps working unchanged.
No CMake surgery — that is what makes this cheap.

Verifiable without a display: sizes are packaging, and cutscene decode checks
headless in the container with the dummy driver.
