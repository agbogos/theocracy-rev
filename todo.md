# todo

Open tasks only. A task leaves this file when it is done — the finding it
produced goes into `docs/`, which stays the record. Nothing here is a plan or a
rationale; if it needs either, it belongs in a doc that this file links to.

Two sections because the split matters: **YOU** is work that needs a machine,
hardware or a judgement only you have. **CLAUDE** is work I can do unattended.

---

## YOU

### 1. Windows on bare metal — blocked on hardware

Two things only bare metal answers, both in
[`docs/porting/other-os-ports.md`](docs/porting/other-os-ports.md), "The
contention runs": whether the ~98 ms loaded province frame is a VM artefact, and
what happens on a machine where nothing has already raised the global timer
resolution to 1 ms. The VM could not answer the second — it reported
`NtQueryTimerResolution current: 1.0000 ms` before the probe started anything,
so it never measured Windows' 15.625 ms default at all.

**That is why the first instruction below is the important one.** Browsers,
Discord, Spotify, media players and game launchers all raise the global timer
resolution process-wide, and any of them running in the background silently
turns the bare-metal run into a second copy of the VM run.

Send the helper this, verbatim:

> 1. **Reboot, and open nothing.** No browser, no Discord, no Spotify, no game
>    launcher. This matters more than it sounds: those programs change a global
>    Windows timer setting, and the whole point of this test is to measure the
>    machine without it.
> 2. Unpack the bundle, open `cmd.exe` in that folder, and paste:
>
>    ```
>    set /a CORES2=%NUMBER_OF_PROCESSORS%*2
>    win-timing-probe.exe                            > probe-idle.txt   2>&1
>    win-timing-probe.exe --busy %NUMBER_OF_PROCESSORS% > probe-busy1x.txt 2>&1
>    win-timing-probe.exe --busy %CORES2%            > probe-busy2x.txt 2>&1
>    powershell -c "Get-CimInstance Win32_Processor | Format-List Name,NumberOfCores,NumberOfLogicalProcessors" > machine.txt
>    ```
>
> 3. **Check one line before going further.** Open `probe-idle.txt` and find
>    `NtQueryTimerResolution current`. It should say about **15.6 ms**. If it
>    says **1.0000 ms**, something was running — reboot and redo step 2 without
>    opening anything first.
> 4. Then one ordinary game session, ten minutes or so, reaching a province with
>    units in it:
>
>    ```
>    set THEOC_FPS=1
>    theoc.bat > session.txt 2>&1
>    ```
>
> 5. Send back `probe-idle.txt`, `probe-busy1x.txt`, `probe-busy2x.txt`,
>    `machine.txt` and `session.txt`.

The probe needs no game data and takes about a minute per run; only step 4 needs
the data tree beside `theoc.bat`, which you have to supply — it is not
redistributable and is not in the bundle.

### 2. Play one signed macOS bundle — interactive only

**The headless half is done (2026-08-21).** The released bundle
boots the dedicated server under Unicorn — `mvos .ctors done: 10 ok, 0 faulted`,
`OpenSubsystems`, socket bound on :5042 — so the hardened runtime does not break
the JIT on a Developer-ID-signed binary, which was the open risk. Notarisation
is live too: `codesign --test-requirement="=notarized"` is satisfied.

What is left is a real session with a display: window, input, save/load,
cutscenes. Nothing about it is expected to differ from a dev build; it is the
last thing no automated check can reach.

### 3. Confirm notarisation before publishing any draft release

CI submits to Apple and does **not** wait — a 30-minute wait timed out once,
burning a runner and discarding the answer. So the build cannot tell you whether
Apple accepted the bundle, and nothing else will either. The submission id is in
the job summary and in the draft release's notes as an unticked checkbox:

```
xcrun notarytool info <id> \
  --key AuthKey_XXXXXXXXXX.p8 --key-id <KEY ID> --issuer <ISSUER UUID>
```

`Accepted` and the draft can go out. Anything else, swap `info` for `log` to get
the reasons. No rebuild is needed if it is accepted late — nothing is stapled,
so the ticket is fetched from Apple at first launch.

---

## CLAUDE

**Refilled 2026-08-20, emptied 2026-08-21** — all three packaging scripts, all
three CI jobs and the third-party manifest are done. Reasoning lives in
[`other-os-ports.md`](docs/porting/other-os-ports.md), "CI: building the bundles
on GitHub".

*(Empty. `THIRD-PARTY.md` landed 2026-08-21 — the manifest is generated into
every bundle by `tools/third-party.sh`, so it cannot drift from what shipped.)*

---

### Historical note — why the bar was set high

**Empty again — 2026-08-15.** It was emptied deliberately on 2026-08-10, took
one task on 2026-08-15 when `reconf` turned up, and that task is done: the
engine's config vocabulary is read, `data/game/mvos.cfg` is corrected, and two
docs that had it wrong are fixed. What is left needs your machine, not mine.

The note below is why the bar is set where it is.

The port has been a release candidate since 2026-08-04 and nothing here
changes it. The RE side reached the point where every remaining question was
archaeology with no consumer: four sessions on 2026-08-10 closed four tasks and
opened four more, leaving the list exactly the size it started. That is what
reverse-engineering does — every answer exposes two things you now realise you
cannot explain — so the list does not converge, and the only way it ends is by
deciding it has.

What was here has **not** been thrown away. Unknowns belong in each doc's own
**"Open threads"** section, which is where `CLAUDE.md` says they live and where a
reader meets them in context instead of as a stray line in a task file:

| what | now in |
|---|---|
| Which men in `init.dat` carry a mission flag; the eight scenario `iMissionHandler` subclasses; `prov+0x400fb`; mission `+0x39` | [`missions.md`](docs/subsystems/missions.md) |
| Bone Horn's slot-5 body; item slots 5–7; items 30 and 40 | [`magic-items.md`](docs/subsystems/magic-items.md) |
| The regen and `*_HIT_PERCENT` keys; `cMan_Comm1`'s subtype byte; where a spell's `+0x350` school is set | [`heroes.md`](docs/subsystems/heroes.md) |
| The unit AI/movement core — the one genuinely large unread mass | [`simulation-step.md`](docs/subsystems/simulation-step.md) |

Refill this section when something has a *reason* — a bug, a port change, a
question you actually want answered — not to keep a queue non-empty.
