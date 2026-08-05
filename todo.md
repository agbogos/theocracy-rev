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

---

## CLAUDE

### 1. Verify the calendar against the code

[`docs/subsystems/calendar.md`](docs/subsystems/calendar.md) is derived from
hex-editing saves, not from the disassembly. Its "Open threads" lists what to
settle; needs **`theocracy.real` open in Ghidra**.

- Find the `date <year> <month> <day>` console handler and read the conversion —
  settles the epoch, the field width and whether the 5 leftover days are a 19th
  month, all in one place.
- Find the date field in the `.tsg`: offset and declared width.
- Check whether the header's text date and the binary count come from the same
  source.

Black-box half, no Ghidra needed: `THEOC_CONSOLE=1`, set a date in the last five
days of a year, see what the game reports back.

### 2. Confirm the video-mode error string

[`docs/reference/original-os-setup.md`](docs/reference/original-os-setup.md)
quotes `Fatal: Unable to activate screen` as verbatim engine output. It is not
in `libmvos.so.0.9` or `theocracy.real` — find which component emits it and from
which call site, and tie it to `SetVideoMode`'s failure path by address.
