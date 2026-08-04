# todo

Open tasks only. A task leaves this file when it is done — the finding it
produced goes into `docs/`, which stays the record. Nothing here is a plan or a
rationale; if it needs either, it belongs in a doc that this file links to.

Two sections because the split matters: **YOU** is work that needs a machine,
hardware or a judgement only you have. **CLAUDE** is work I can do unattended.

---

## YOU

### 1. Windows: re-run the rebuilt bundle — VM, 2 min

`dist/theoc-windows-x64/` has been rebuilt with the `WSAStartup` fix. Copy it
over and run it. Expected in the log, right after `[start]`:

```
  [net] socket(type=1) -> guest fd 4
  [net] bind(:5043) faked OK — single-instance lock
```

If instead you see `[net] socket(type=1) FAILED -> linux errno N`, send me N —
the diagnosis was wrong and that number says what is actually happening.

This supersedes the cutscene check: the previous bundle never reached a cutscene,
so while you are in there, confirm the intro plays with **video and sound** (the
minimal ffmpeg's failure mode is silent — the port logs `[smpeg] decode failed,
will skip frames` and carries on to the menu).

### 2. Windows: re-verify the netgame — VM, ~10 min

The netgame was verified by play on 2026-08-04, but on a binary where sockets
only worked because a full-fat ffmpeg DLL had initialised Winsock for it. That
verification does not carry over to the current build. Two instances on the one
VM is enough — the single-instance lock is faked precisely so that works.

See [`docs/porting/other-os-ports.md`](docs/porting/other-os-ports.md),
"The same Fatal, a second time".

### 3. Windows on bare metal — blocked until ~2026-08-18

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

Nothing queued.
