# Diagnostics: the instruments, and the method they encode

This is the single catalogue of every diagnostic the port carries — the ~38
`THEOC_*` environment knobs, the instruments that are always on and are not env
knobs at all, and the debugging method the whole set exists to serve.

Everything here was read out of `port/src/` (`traps.cpp`, `main.cpp`,
`machine.cpp`, `video.cpp`, `guestlink.cpp`, `blit.cpp`, `mvos.cpp`) and
cross-checked against the commit history (`python3 tools/dump_commit_log.py`
→ `data/commit-log.md`, which is untracked and regenerated on demand). Defaults
and units are taken from the code, not from prose. Almost nothing here was
verified by running the game — it needs a display and the copyrighted data tree.
The exceptions are `THEOC_CONSOLE` and `THEOC_EDIT`, both exercised live.

---

## The method: what to ask first

Three questions decide almost every investigation here, and each has an
instrument that answers it in one line. Reaching for a profiler, a disassembler
or a hypothesis before answering them is how this project has repeatedly lost a
debugging cycle.

**1. Is the emulator saturated, or idle while slow?** Under this emulator
wall-clock-shaped bugs masquerade as performance bugs. Province view once sat at
a rock-steady 11.9fps; the native LFB16 rasteriser removed real per-frame CPU
cost and *did not change the frame rate at all*, because the bottleneck was a
stalled game clock, not the renderer. The decisive question is never "what's
hot?" but "is the emulator busy?" — if it is idle while slow, the cause is a
host-side wait we synthesised (a sleep, a signal we failed to deliver, a clock we
advanced too slowly), not the guest's compute. `THEOC_FPS=1` prints guest basic
blocks per second next to the frame rate and answers it in one line. See
[frame-timing.md](frame-timing.md).

**2. On a hang: is the guest still executing?** A freeze is ambiguous only until
you know which side is stuck. `THEOC_WATCHDOG` is the first thing to reach for on
any "it froze": it watches the present counter from a host thread, and when
frames stop it samples guest blocks *and* trap calls over half a second and says
which side is wedged — **still running (spinning)** with the guest EIP that is
spinning, or **not executing (stuck host-side)** with the name of the last trap
entered. It turns "it hung" into an address. `THEOC_SLOWLOG` is the follow-up:
the watchdog says *that* we are stuck host-side, `THEOC_SLOWLOG` says *which
handler*.

**3. Am I measuring, or inferring?** The map-selection crash cost three
confident wrong diagnoses in a row — a stack slot read as a return address, a GOT
value read from the file on disk instead of guest memory, and a truncated grep
taken as proof of a missing export — before an instrument found the real cause in
one run. When a claim is about guest state, read guest state.

---

## The complete `THEOC_*` catalogue

38 variables. **A note on parsing that bites:** most are gated on *presence*
only, so `THEOC_WATCHDOG=0` still turns the watchdog on. The only three that
inspect their value for an off-switch are `THEOC_NATIVE_BLIT` (off only on a
leading `0`), `THEOC_FULLSCREEN` and `THEOC_NO_HIDPI` (off when unset, empty, or
exactly `"0"`). Numeric knobs parse with `atoi`/`atof`, so garbage reads as 0 and
then falls into whatever that variable's zero case is.

### Instruments

| Variable | Argument / units | Default | What it does |
|---|---|---|---|
| `THEOC_FPS` | presence | off | Per-second `[fps]` line on stderr: fps, guest blocks/s and blocks/frame (the saturation check), heartbeat and mixer redirect rates, `usleep` ms/s and call count, `gettimeofday`/s, `select`/s, audio queue depth in seconds and underruns/s, guest heap live MB and frontier growth MB/s. The tool that split throughput-bound from timing-bound; its heap column also caught the G14 `cIntuition` corruption. |
| `THEOC_PROFILE` | presence | off | Size-weighted guest basic-block histogram (Σ instruction bytes ≈ work), rolling top-15 dumped every 3s so the window tracks whatever is on screen. Host trap/stub/scratch pages (≥ `0x50000000`) are excluded; addresses are labelled `game 0x…` / `mvos+0x…` for the two Ghidra DBs. Armed just before `Start`, so boot and `.ctors` are not in the sample. Found the hot blit functions. |
| `THEOC_TRACE` | presence | off | 32-entry ring of the last basic-block entries, dumped (oldest-first, labelled) when `Start` faults. Essential exactly when the EBP walk cannot help: at `eip=0` the frame pointer is usually 0 too, and this is then the only thing that shows how control got there. |
| `THEOC_WATCHDOG` | seconds | off; **10s** when the value is ≤ 1 | Host thread, armed on the first present. Polls the present counter every 250ms; after the given stall it samples `exec_blocks` and the trap sequence over 500ms and reports uptime, stall length, guest running/not-running, the last guest EIP, the last trap name and live heap. **Arms the guest block counter itself** — its whole verdict is read off `exec_blocks`, and that counter used to be armed by `THEOC_FPS` alone (see "A counter nobody armed" below). |
| `THEOC_WATCHDOG_SAMPLE` | path | off | On a **host-side** stall only (guest not executing), shells out to `sample <pid> 1 -file <path>` to capture a native stack of exactly that moment. An aggregate profile over a 40s run cannot isolate a 1.5s window. |
| `THEOC_SLOWLOG` | milliseconds | off; **250ms** when the value is ≤ 1 | Prints `[slow] <section> took N ms` for any host-side section that blocks the emulation thread past the threshold. Covers every trap dispatch, every plugin dispatch, `OpenDisplay` and `present`. The deliberate frame-cap sleep is credited out, or every capped frame would report as an 83ms "slow" section and bury the real ones. |
| `THEOC_KEYLOG` | presence | off (first 24 keys only) | Logs every key event for the whole session as `[input] key eKey=0x.. down sc=N quals=0x..`, instead of the 24-event boot budget. The question it answers is the one a broken shortcut always poses: **did the chord reach the guest at all?** A chord that never arrives and a chord that arrives and is ignored look identical on screen and need opposite fixes — a host input bug versus a game gate. `quals` is the SDL modifier state, so a swallowed Alt is visible too. |
| `THEOC_REPORT_CLICKS` | presence | off | Logs every mouse-button-down as `[click] x,y btn= win=WxH screen=0x…`, in a form that pastes straight into a `THEOC_CLICKS` path. The active `cScreen*` (`Intuition+0x24`) doubles as a screen identity — clicks sharing that value are on the same screen. |
| `THEOC_LOUD_ABORT` | presence | off | Default policy is bring-up-friendly: guest `abort()` (the tail of `Fatal()`) logs and returns so the caller continues past non-critical Fatals. Set this and `abort` instead walks the g++ 2.95 EBP chain (max 24 frames, labelled) and `request_stop()`s the current call, so a real fault surfaces at its origin instead of hiding as a silent `OpenSubsystems` restart. |
| `THEOC_HEAP_TEST` | presence | off | Runs the guest allocator's randomized alloc/free/realloc self-test standalone and exits (arena mapped, nothing else allocated). It guards the failure that would be worse than a leak: two live blocks overlapping. Deliberately does not continue into boot — it leaves the arena fragmented. |
| `THEOC_TREE` | presence | off | One-shot dump of the `cVObject` widget tree (node address, vtable, x/y/w/h, `(empty)` when fully clipped) during the native `PaintTree` walk. **Dead in the current build:** it lives in `port/src/mvos.cpp`, the legacy pure-HLE MVOS layer, which `port/CMakeLists.txt` has commented out of the target. Previously undocumented. |

### Self-drivers and harnesses

| Variable | Argument / units | Default | What it does |
|---|---|---|---|
| `THEOC_SOAK` | cycles | off; **5** when the value parses ≤ 0 | Drives menu → Prophecy → OK → province → map → exit → confirm → menu repeatedly, printing a `[soak]` resource snapshot (heap live/frontier, host RSS, guest ESP, stub bytes, open fds) at the same point in every cycle. Steps wait on the active `cScreen*` changing, not on a stopwatch; each carries a deadline (90/90/60/60/60s) and fails loudly with a snapshot. This is the load/unload pattern that found G15. |
| `THEOC_SOAK_PLAY` | seconds | **20** (also when the value parses ≤ 0) | Province dwell inside each soak cycle. |
| `THEOC_CLICKS` | `"x,y;x,y;…"` | off | Click path for the render-bug harness. 3s settle before the first click, 2s between clicks, each click paced aim → press → release three frames apart. |
| `THEOC_MOUSE_SWEEP` | presence | off | After the click path finishes, drags the pointer across the screen a few pixels per frame (7px/frame horizontally, sine vertically) so a failed background restore leaves a visible track. |
| `THEOC_SHOT_EVERY` | N frames | off | Saves every Nth presented frame as `<dir>/frame_%03d.bmp`, capped at 40 files. Also fires from the cutscene present path (`SMPEG_playvideoframe`), which is capture-only — a synthesized click there would skip the thing being photographed. |
| `THEOC_SHOT_DIR` | path | `.` | Destination directory for the above. |
| `THEOC_AUTO_PROVINCE` | presence | off | Self-drives menu → Prophecy (80,260) → OK (466,537) into province view on a wall clock (steps at 1.5/1.7/1.9s and 3.5/3.7/3.9s), for unattended timing tests. Wall-clock rather than frame-counted because fps varies wildly across screens. One-way trip — it cannot cycle; that is what `THEOC_SOAK` is for. |
| `THEOC_AUTO_MENU` | presence | off | Bring-up driver: once an 800×600 menu has presented 45 frames, synthesizes aim/down/up on the Single Player button (80,260; `menu.cfg` "single 20 250") at frames 45/50/55. Guarded on `width()==800` so it cannot fire on another screen. |
| `THEOC_LONGRUN` | seconds | off; **60s** when the value parses ≤ 0 | The multi-hour session harness. Prints a four-line `[health]` snapshot on that interval — wall-clock time (so an out-of-band note like "reloaded a save at 21:44" can be lined up against the samples), uptime, fps and the frame cap in effect, **guest blocks/s and blocks/frame** (the same saturation check `[fps]` gives, so a slow interval can be classified rather than guessed at), **live-set growth** since start and per interval, the frontier as a level with its own rate and the resulting arena headroom (**withheld as `n/a (warm-up)` for the first 0.5 h**, because the since-start frontier rate is dominated by the one-time ~27 MB scenario load: a ten-minute trial otherwise reports a terrifying "+6498 MB/h -> 0.0 h headroom" while the live set is dead flat), host RSS delta, guest ESP, stub bytes, open fds, audio queue depth and underrun frames, and how many log lines have been suppressed. Also **rate-limits repeatable log lines** (`[slow]`, ignored aborts: a burst of 5 then one per 60s, with the dropped count surfaced in `[health]`), so a stuck condition cannot write gigabytes overnight. **`Alt+M` stamps a numbered `[mark]` line into the log** and forces the next `[health]` out immediately, so an interval boundary lands on the event instead of wherever the timer was; the hotkey is live only while this harness is armed, because Alt is a modifier the game itself uses. Arms `THEOC_WATCHDOG=30` and lifts `THEOC_START_SEC` to unlimited, each unless set explicitly — the Start budget's 600s default otherwise caps a multi-hour session at ten minutes. Everything goes to **stderr**, like every other instrument, so `2>log` captures the whole session. |
| `THEOC_AUTO_KEYS` | presence | off | Taps SPACE (down, then up 0.2s later) every 6s through the real SDL event path, from both present sites so it also fires during cutscenes. The mouse self-drivers never press a key, so the keyboard half of the input path had no unattended coverage — and SPACE is exactly the key that wedged `cIntuition::PushKeyInput`. |
| `THEOC_SERVER` | presence | off (boots `data/cd/linux/theocracy.real`) | Boots the shipped dedicated server `data/cd/linux/server` instead — same host, same linker, same HLE. Headless is *derived*, not declared: `server` carries no `_12cApplication.Video` requirement flag, so video/input/blit bring-up is skipped automatically. |
| `THEOC_START_ANYWAY` | presence | off | Calls `Start__12cApplication` even when `OpenSubsystems` did not return cleanly. For bringing up a boot path that dies in subsystem open, when you want to see how far the game itself gets. Previously undocumented. |

### A/B reverts (escape hatches for a landed fix)

Each of these restores the behaviour a specific fix replaced, so the fix can be
A/B'd against the bug it cured rather than argued about.

| Variable | Argument | Default | Reverts |
|---|---|---|---|
| `THEOC_LEGACY_SLEEP` | presence | off | The heartbeat fix. `usleep` goes back to a blind sleep, with the 30Hz tick and the sound slice serviced only at present — i.e. the present-coupled clock that pinned province at 12fps. |
| `THEOC_LEGACY_KEYMB` | presence | off | The cutscene-skip key mailbox. Never posts, so intros become unskippable. A/B switch for input-path hangs. |
| `THEOC_LEGACY_SPRITE` | presence | off | The single-buffer `cSprite::AfterSwapBuffer` patch. Restores the double-buffer slot swap, i.e. the cursor-trail bug on static screens. |
| `THEOC_NATIVE_BLIT` | `0` disables | on | The native LFB16 blit overrides (`PutBitmap8C1_AMask`, `PutBitmap8`, `PutBitmap8_AMask`, `PutBitmap`, `VLineAlfa`); `=0` falls back to the emulated libmvos rasteriser. **Only a leading `0` turns it off.** |
| `THEOC_REAL_LOCK` | presence | off | The faked single-instance lock. `bind()` on port 5043 is normally faked OK so two clients can run on one Mac; setting this honours it for real, which is also the proof that the socket transport and errno translation work end to end (the second instance gets a genuine `EADDRINUSE` and the guest says "You can run only one Theocracy in the same time!"). |
| `THEOC_LEGACY_SCALE` | set, non-empty, not `"0"` | off | Sharp-bilinear presentation. Reverts to integer scale + nearest — the G18 behaviour, perfectly hard pixel blocks and a ~5% area loss to the integer floor. The same fallback engages automatically if the renderer reports no `SDL_RENDERER_TARGETTEXTURE` or the 3× intermediate cannot be created, with a `[video]` line saying so. |
| `THEOC_NO_HIDPI` | set, non-empty, not `"0"` | off | `SDL_WINDOW_ALLOW_HIGHDPI`, in both windowed and fullscreen. Reverting means macOS hands SDL the window's point size and the OS upscales again — two resamples. |

### Configuration

Not diagnostics, but they shape every run and belong in one list.

| Variable | Argument / units | Default | What it does |
|---|---|---|---|
| `THEOC_DATA` | path | `data/game` | Install root. Guest paths `data/…` resolve under it. |
| `THEOC_CD` | path | `data/cd` | CD root. `/mnt/cdrom/*` remaps here (`VM_GetCDRomName` opens `cd.key` and checks for "Theocracy"), and `movie/*.mpg` / bare `*.mpg` resolve to `<cd>/movie/…`. Previously mentioned only in a parenthetical. |
| `THEOC_SKIP_MOVIES` | presence | off | `SMPEG_new` succeeds without the file and never decodes; status goes straight to STOPPED. Fast boot. |
| `THEOC_FRAME_MS` | milliseconds | **83** (= 12fps, `cProvince::Do`'s own `0x14585` µs limiter); `0` disables | Minimum present-to-present interval. Physics and animation are frame-tied, so uncapped render is turbo sim. Only too-fast frames are slowed; the game's own `usleep` pacing counts toward the interval and is not double-limited. |
| `THEOC_AUDIO_MS` | milliseconds | **120** | Target mixer queue depth, which *is* the audio latency. Too high delays SFX, too low re-introduces underrun stutter. |
| `THEOC_START_SEC` | seconds | **600**; `0` = unlimited; negative clamps to 0. **`THEOC_LONGRUN` defaults it to `0`** unless set explicitly | Host wall-clock budget for the entire `Start()` call (intros + menu + play). Not an in-game timer. A timeout while the window is still open is reported as "host Start timeout — still in game" and counted as a live session, not a failure — but it *reads* like a fault, so suspect it first whenever a session ends at a suspiciously round elapsed time with nothing wrong in the log. |
| `THEOC_VIDEO_HOLD` | seconds | **2** | How long the window is held open after `Start` returns, so a final frame can be read. Previously visible only inside a sample command line. |
| `THEOC_SCANLINES` | percent, 0–90 (clamped) | **0** (off) | CRT-lite: darkens one row in every three of the sharp-bilinear intermediate, which is exactly one dark line per guest pixel row regardless of window size. 25 is a light hint, 60 heavy. A taste knob, off by default — scanlines are polarising and cost brightness. No effect under `THEOC_LEGACY_SCALE` or during cutscenes, neither of which uses the intermediate. |
| `THEOC_FULLSCREEN` | set, non-empty, not `"0"` | off | Borderless fullscreen at the desktop resolution (`FULLSCREEN_DESKTOP`, never an exclusive mode switch); 4:3 is preserved with pillarbox bars. Falls back to windowed if creation fails. `Alt+Enter` (⌥Return) toggles at runtime. |
| `THEOC_CONSOLE` | presence | off | Arms the in-game developer console — **Alt+V** opens, **Alt+C** closes, on both the realm and province screens. No game patching: Alt+V is captured in the SDL hook and serviced at the next present as a guest call to `Edit__10cVOConsole(g_LogConsole)`, through the same one-redirect-per-present path the timer and sound slices use. Refused unless a `cShell` is attached (`g_LogConsole+0x38`), which is true exactly while a game screen is live. Skipped for headless images. Full chain: [../subsystems/dev-console.md](../subsystems/dev-console.md). |
| `THEOC_EDIT` | presence | off | Forces the game's own **edit mode** on (`g_GameSession+0x50`, the byte `cGameSession.md` used to call `bPaused`). The mode exists in the binary but no shipped path selects it — every `SetupGame` call site passes normal. **It freezes the simulation**, which is what edit mode *is*, and is what makes the console `save` command legal. Re-applied per present because the game builds a new `cGameSession` on every load and re-initialises the flag; 58 sites read it and none writes it back, so stamping is sufficient. Independent of `THEOC_CONSOLE`, though `save` is the main reason to want it. See [../subsystems/dev-console.md](../subsystems/dev-console.md#edit-mode). |

---

## Which instrument for which symptom

| Symptom | Reach for |
|---|---|
| **It froze.** | `THEOC_WATCHDOG=1` first — it tells you which side is stuck. Guest still running → the reported EIP is the spin; take it to Ghidra. Guest not executing → add `THEOC_SLOWLOG=250` to name the handler, and `THEOC_WATCHDOG_SAMPLE=/path/stack.txt` for a native stack of exactly that moment. |
| **It's slow.** | `THEOC_FPS=1`. Blocks/s high and flat → genuinely CPU-bound; then `THEOC_PROFILE=1` for the hot blocks. Blocks/s low while fps is low → a host-side wait, and the `usleep` ms/s, heartbeat/s and mixer/s columns on the same line say which. Drive it there unattended with `THEOC_AUTO_PROVINCE=1`. |
| **It crashed at `eip=0`.** | The zero-GOT scan (always on) is already in the log — if it says 0 slots, it is *not* an unresolved import. Then `THEOC_TRACE=1`, because at `eip=0` the frame pointer is normally 0 and the EBP backtrace prints "no frame pointer". `eip=0` with `EBP=0` means a smashed frame; look for who wrote past a buffer. |
| **Audio stutters.** | `THEOC_FPS=1` — `underrun=N/s` counts callback samples pulled from an empty queue, and `audio q=` is the current depth in seconds. Raise `THEOC_AUDIO_MS` to trade latency for margin; `THEOC_LEGACY_SLEEP=1` to confirm whether the fix that decoupled the mixer from the frame rate is what is holding it together. |
| **A visual bug.** | Frames, not logs. `THEOC_SHOT_EVERY=N` + `THEOC_SHOT_DIR` to capture (it covers cutscenes too), `THEOC_CLICKS="x,y;x,y"` to drive to the screen, `THEOC_MOUSE_SWEEP=1` when the bug needs a moving pointer across consecutive frames. Lift the coordinates with `THEOC_REPORT_CLICKS=1` first. For geometry and scaling, read the `[video]` line before looking at the screen. |
| **A multi-hour session.** | `THEOC_LONGRUN=60`, redirect stderr to a file, then plot it: `python3 tools/plot_health.py session.log`. **Press `Alt+M` whenever the activity changes** — battle, reload, panel, idle. Without markers a session is one undifferentiated slope and every segment boundary is a guess; with them the tool prints a per-segment table (fitted MB/h, MB/1k frames, mean fps and blk/frame between one marker and the next) and rules them onto the chart. A controlled trial is one marked segment. Don't read 137 samples as text — the question a long session answers is about *slope*, and the tool fits one (and prints the same numbers as a table with `--table`). All growth figures are on the **live set**; the frontier is reported as a level only, because it is a high-water mark that stops moving once freed blocks are reused. `interval` catches a sudden onset, `avg` a slow leak — the average includes the one-time ~29 MB scenario load, so give it ~30 min. **Growth per 1k frames is the figure to compare across runs**, because the engine is frame-tied: a session at `THEOC_FRAME_MS=50` (20fps) steps the simulation ~1.67× faster than the 83ms default and so allocates ~1.67× as much per wall-clock hour while being no less correct. |
| **It leaks over a long session.** | `THEOC_SOAK=20 THEOC_SOAK_PLAY=20` and compare the per-cycle `[soak]` snapshots — the numbers to watch are heap live vs frontier, host RSS, guest ESP, stub bytes and fd count. `THEOC_FPS`'s heap column gives the same split live-in-flight. `THEOC_HEAP_TEST=1` if the allocator itself is suspect. **Watch `live`, not `frontier`** — see the note below. Note there is no allocation-site histogram; attributing a slow leak would need one built. |
| **A missing import.** | The trap report at exit prints `UNIMPLEMENTED hit: N` with a call-count-sorted list of names — but only for imports that were *called*, so a path you never drove reports nothing. The zero-GOT scan after linking is the complement: it names every JMP_SLOT/GLOB_DAT slot still holding 0, before anything calls through it. `[link] unresolved strong UND` from `resolve()` is the third, and it only fires for STRONG symbols. |

### Live set vs. frontier — why the growth figures moved

The guest heap has two numbers and only one of them answers "is this leaking":

- **frontier** — how far the bump allocator has ever reached. A **high-water
  mark**: it cannot fall, and it stops rising the moment freed blocks satisfy
  new requests.
- **live** — bytes currently allocated. Rises on a leak, *falls* on a teardown
  or a save reload.

`[health]` originally derived every growth figure from the frontier. That was
sound before G15, when `free()` was a no-op and the two moved together — and
silently wrong after it. Measured on a 2.28 h session (2026-07-27): **105 of 137
samples reported `interval +0.000 MB/h`** while the live set climbed **+7.2
MB/h**, dead linear. The instrument built to find a leak read zero straight
through one.

So growth is now measured on `live`, signed, and the frontier is reported as a
level with its own rate — because headroom against the 128 MB arena genuinely
*is* a frontier question (it is the frontier that runs into the end of it).

**The general lesson**, which is the same one `frame-timing.md` teaches about
clocks: when an instrument reads exactly zero, confirm it *can* be non-zero
before believing it.

### A counter nobody armed

The same lesson, caught again on 2026-08-01 and worth its own note because the
zero this time was in the *other* direction — an instrument that always gave the
same confident answer rather than an obviously dead one.

`Machine::exec_blocks()` only counts if a block hook was installed, and the hook
was installed by `THEOC_FPS` alone. Three instruments read the counter:

- `[fps]` — armed it itself, so it was always right.
- `[health]` — did not report it at all, which is why the 2026-07-31 session
  cannot say whether its 11.5 fps battle was the guest doing more work or the
  host falling behind. It reports it now.
- **the watchdog** — its entire verdict is `db ? "STILL RUNNING (spinning)" :
  "NOT EXECUTING (stuck host-side)"`. With the counter unarmed, `db` is 0 every
  time. A `THEOC_WATCHDOG=30`-only run — the documented first reach on "it froze"
  — would therefore have called *every* stall host-side, whatever the guest was
  doing, and sent the reader to `THEOC_SLOWLOG` for a handler that was not the
  problem. No stall has fired on such a run yet, so nothing was misdiagnosed;
  the bug was found by reading the code, not by being burned by it.

`THEOC_LONGRUN` and `THEOC_WATCHDOG` now arm the counter themselves. It costs one
relaxed increment per basic block, and that does cost frames — which is the
standing reason growth is reported per 1k frames as well as per hour.

**The lesson to carry:** a shared instrument reads state that something *else*
switched on. Check the arming path from every consumer, not just the one you
built it for.

---

## The instruments that are not env vars

These are always on. They cost nothing and they are the ones that catch the
failures nobody thought to switch an instrument on for.

**Zero-GOT scan (after linking, `guestlink.cpp`).** Walks every
`R_386_JMP_SLOT`/`R_386_GLOB_DAT` relocation in both images and reports any slot
still holding 0, by symbol name, then prints the total. A zero slot means a call
through it jumps to address 0 and faults with `EIP=0` and no frame pointer — one
of the least diagnosable failures possible, and easy to mistake for a null vtable
or a smashed stack. It exists because `resolve()` only warns for STRONG undefined
symbols, so weak ones could land there silently. Its first run reported zero
slots, which definitively killed a GOT hypothesis that had already been retracted
once on inference.

**EBP-chain guest backtrace on fault (`main.cpp`, `print_guest_backtrace`).**
`Machine` captures EBP at fault time; the fault site walks the g++ 2.95 frame
chain (up to 24 frames, bailing when frame pointers stop ascending) and labels
each return address `game 0x08…` for `theocracy.real` or `mvos+0x…` for libmvos
file offsets, so every frame drops straight into the right Ghidra DB. It replaced
a dump of 16 raw stack words that left the reader to pick out a return address by
eye — a guess that sent one investigation down a completely wrong path. The same
walk is what `THEOC_LOUD_ABORT` prints. When it says "(no frame pointer)", that
is itself the finding: go to `THEOC_TRACE`.

**Trap report at exit (`TrapLayer::report`).** Counts imports that were actually
hit, split into implemented (with total calls) and **UNIMPLEMENTED** (with total
calls, then listed by name, most-called first), followed by guest heap live MB,
frontier MB, arena MB and free-block count. `0 unimplemented` is the standing
regression bar for every commit. **On stderr**, like every other instrument — it
was on stdout until 2026-08-01, which meant the documented `2>session.log`
capture recipe dropped it: the 2026-07-31 two-hour session exited normally and
still has no end-of-run allocator state, because it went to a terminal. `Mvos::report` adds a vtable-slots-hit line in
the legacy layer. `main` prints the matching `.ctors` tally (ok / aborted /
no-return / faulted).

**The log lines.** `[fps]` is the per-second frame instrument above. `[video]`
reports the real geometry — guest size, renderer output in pixels *and* window
size in points, `hidpi on/off`, crisp/smooth, scale factor, pillarbox and
letterbox bar widths, depth code — so "why are there bars" and "why is it blurry"
are answerable from the log rather than from the screen; equal px and pt is the
tell that HiDPI silently did not engage. `[heap] OUT OF MEMORY` prints the
request size against live, frontier and arena. `[slow]`, `[soak]`, `[watchdog]`,
`[link]`, `[click]`, `[net]`, `[smpeg]` and `[HLE]` mark their own subsystems;
the soak driver switches stdout to line buffering on start so guest prints and
our stderr diagnostics interleave in the right order.

### Which stream: stdout is the guest's, stderr is ours

Settled 2026-08-02, after the same defect surfaced a third time.

- **stdout** — the *guest's* output, and nothing else. Four sites, all in
  `traps.cpp`: the `puts` and `printf` handlers, and the two write paths that
  honour the guest's own fd (`fd == 2 ? stderr : stdout`).
- **stderr** — everything the *port* says: every `[tag]` line, the boot
  narrative, the `.ctors` tally, the trap report.

The rule exists because the documented way to capture a session is
`2>session.log`, so anything of ours on stdout is **absent from the log the
analysis is done on** — while still being visible on the terminal during the
run, which is exactly what makes it easy to miss. It has now cost three
measurements:

| When | What was lost |
|---|---|
| 2026-07-31 | `TrapLayer::report()` — the two-hour session exited normally and has no end-of-run allocator state |
| 2026-08-01 | `[video]` and `[click]` — trial 2's +1.08 MB step could not be attributed to a fullscreen toggle from the log; the operator had to remember it |
| 2026-08-02 | `[console]` and `[edit]` — would have made console use invisible during the battle trials |

The first two were fixed one site at a time, which is why there was a third. All
115 host `printf` sites across `port/src` now write to stderr, and the split is
stated in `traps.cpp` above `register_builtins` so the next diagnostic starts on
the right stream. Verified with `THEOC_HEAP_TEST=1`, which needs no display:
**stdout is 0 bytes**.

The general form is the same as "A counter nobody armed" above — an instrument
is only as good as the path that delivers it, and that path is worth checking
from the consumer's end rather than the author's.

---

## Lessons the instruments encode

**An instrument that infers liveness from a counter must be disarmed wherever
that counter legitimately stops advancing.** The stall watchdog reported a >2s
host-side stall entering the province. It was measuring its own shutdown: once
`Start` returns, the window hold presents through `Video::keep_open_for`, which
never touches the present counter the watchdog reads — so "no frames + guest not
executing" fired on a process that was simply exiting. A captured stack showed
`~TrapLayer`. `main` now calls `stop_watchdog()` before the wind-down, with the
comment "frames stop legitimately now". The same commit closed the mirror-image
gap: `dispatch_plugin` was not updating the watchdog counters at all, yet it hosts
the heaviest host-side work in the port (`OpenDisplay`, `SwapBuffers`/present), so
"stuck inside present" was indistinguishable from "stuck nowhere".

**A silently rejected input is unfalsifiable from the log, so failure paths must
dump their raw bytes.** The engine never sets `sin_family` — the single-instance
lock binds `{family=0, port=5043, INADDR_ANY}`. Linux tolerates `AF_UNSPEC` on a
socket already created `AF_INET`; BSD returns `EAFNOSUPPORT`. The first cut of
`guest_to_host_sin` returned −1 silently, which surfaced as an unexplained
`Fatal` three layers up in guest code and cost a debugging cycle. The failure path
now always dumps the raw guest sockaddr. The same lesson recurs in the linker:
`R_386_JMP_SLOT` used to be `m.w32(P, S)` with no check, so an unresolved symbol
silently wrote 0 to the GOT and the trap report kept reporting 0 unimplemented
while the game died on a missing import — which is why the zero-GOT scan exists.

**`eip=0` with `EBP=0` is a smashed frame, not a null call.** Linux/i386
`struct stat` (`_STAT_VER_LINUX`) is exactly 88 bytes and callers put it on the
stack; `__xstat` was writing 96 zeroed bytes with a guessed layout, so every call
ran 8 bytes past the caller's local and zeroed the saved EBP and return address
sitting immediately after it. The victim was `cDirent::cDirent` (`mvos+0x4c030`),
which calls `__xstat` twice: it completed normally and then `ret`-ed to 0 with EBP
popped as 0 — a fault several frames from the actual damage, and only on the
netgame map dialog, the one path that constructs a `cDirent`. Three inferences had
already been stated more confidently than the evidence supported. What found it
was instruments: the zero-GOT scan reported 0 slots and killed the GOT theory, and
the `THEOC_TRACE` ring then showed that `mvos+0x4c1e8` was a function *epilogue*
rather than a call site — which reframed the whole thing — with two trap slots in
the trace decoding to `strrchr` and `__xstat`. Look for who wrote past a buffer,
not for an unresolved symbol.

**A self-driver's clicks must be paced across frames.** At the 12fps province
cadence a frame is 83ms and the game samples the pointer once per
`ProcessInputs`. The soak driver's first cut did aim+press in one frame and
released 80ms later — under one frame — so the press was never observed, and the
shakedown timed out having never left the menu. Clicks are now aim → press →
release, three frames apart. The related rule is that steps wait on an observable
transition, not a stopwatch: the active `cScreen*` at `Intuition+0x24` changes on
every screen change, so "click, then wait for that pointer to differ" survives a
slow load, where a wall-clock script would desync every later click onto the
wrong screen. Every step still carries a deadline, because a step that never
completes is a bug to report loudly, not a driver that hangs silently.

**A harness with a blind spot is worse than no harness where the bugs live.**
Frame capture originally ran only from the normal present path, but cutscenes
present from `SMPEG_playvideoframe` — so the harness was blind over exactly the
frames a video-scaling bug shows up in. `shot_tick` was split out of
`render_probe_tick` so the cutscene path can drive capture *without* the click and
sweep drivers, which would skip the cutscene being photographed.

**Deliberate waits must be credited out of a "slow" measure.** The frame cap
sleeps up to 83ms on purpose. Without `slow_credit_ms_`, every capped frame would
report as an 83ms `[slow]` section and bury the real ones.

---

## Cross-references

- [frame-timing.md](frame-timing.md) — the present-coupled heartbeat and
  frame-tied simulation, and how `THEOC_FPS` diagnosed both.
- [host-architecture.md](host-architecture.md) — where these instruments sit in
  the host, and the invariants they protect.
- [../reference/re-methodology.md](../reference/re-methodology.md) — the other
  half of question 3 above: the reading errors these instruments exist to catch,
  and how to avoid making them in the first place.
- [guest-libmvos.md](guest-libmvos.md) — per-gate writeups (G13 loud abort, G14/G15
  heap, G16 watchdog and cutscene skip, G17 render probe, G18 fullscreen, G19
  sockets, G20 server) that these instruments were built during.
- `task_fifo.md`, "Notes" — the running list these tables consolidate.
- `data/commit-log.md` (generate with `tools/dump_commit_log.py`) — the commit
  for each instrument states the problem that
  forced it into existence.
