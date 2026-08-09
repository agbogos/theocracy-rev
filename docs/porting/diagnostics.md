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

## Setting them permanently: `theoc.cfg`

Since 2026-08-08 the same names can live in a file instead of the environment,
so a release bundle can carry settings. `port/theoc.cfg` is the shipped
template; both packaging scripts copy it to the top of the bundle and point
`THEOC_CONFIG` at it from the launcher, because the binary sits in `bin/` and
would otherwise look in the wrong place.

Format is `NAME = value` with `#` comments — deliberately the shape the game's
own `mvos.cfg` already uses, and deliberately **not** TOML or YAML: the knobs
are a flat list of scalars, so a structured format would buy nothing and cost
either a vendored dependency or a parser that implements a fraction of a spec
while claiming the whole thing. `[section]` headers are accepted and ignored,
keys are case-insensitive, and a UTF-8 BOM is stripped (Notepad writes one).

The rules that matter:

- **The environment always wins.** `THEOC_MUSIC_VOL=50 ./theoc` beats the file,
  so the file can never make a command line lie.
- **No file, or an unreadable one, is exactly the old behaviour.** This must
  never become a new way for the game to fail to start.
- **An empty value is "leave it unset", not "set it to empty".** Most knobs are
  presence-tested, so `THEOC_FPS =` turning the instrument *on* would be the
  opposite of what anyone writing that line means.
- **A misspelt `THEOC_*` name is applied anyway, with a warning.** Failing shut
  would mean a knob added to the code but not to the checker silently stops
  working; this way that mistake is cosmetic.
- **Four are refused from the file** — `THEOC_FIX_SAVE`, `THEOC_HEAP_TEST`,
  `THEOC_SERVER` and `THEOC_TREE`. The first three replace running the game, so
  a stale line means it never starts and the person hitting that is the least
  equipped to work out why; `THEOC_TREE` is dead code. Environment only, and
  each one is logged as refused rather than ignored quietly.

Search order: `$THEOC_CONFIG`, then `theoc.cfg` beside the executable, then
`./theoc.cfg`. The chosen file and its setting count are logged, right after the
build-identity banner.

The template ships **15 settings, all commented out** — display, speed, sound,
startup, paths, and the three enthusiast knobs (`THEOC_CONSOLE`, `THEOC_EDIT`,
`THEOC_REAL_LOCK`). The other 33 are accepted from the file but not listed in
it: a config a player scrolls past is one they do not read.

---

## The complete `THEOC_*` catalogue

49 variables. **A note on parsing that bites:** most are gated on *presence*
only, so `THEOC_WATCHDOG=0` still turns the watchdog on. The only three that
inspect their value for an off-switch are `THEOC_NATIVE_BLIT` (off only on a
leading `0`), `THEOC_FULLSCREEN` and `THEOC_NO_HIDPI` (off when unset, empty, or
exactly `"0"`). Numeric knobs parse with `atoi`/`atof`, so garbage reads as 0 and
then falls into whatever that variable's zero case is.

### Instruments

| Variable | Argument / units | Default | What it does |
|---|---|---|---|
| `THEOC_FPS` | presence | off | Per-second `[fps]` line on stderr: fps, guest blocks/s and blocks/frame (the saturation check), heartbeat and mixer redirect rates, `usleep` ms/s and call count, `gettimeofday`/s, `select`/s, audio queue depth in seconds and underruns/s, guest heap live MB and frontier growth MB/s. The tool that split throughput-bound from timing-bound; its heap column also caught the G14 `cIntuition` corruption. **Since 2026-08-03 the fps figure is not a proxy for sim rate**: the async-cursor path presents out-of-band, so province reports ~30 presents/s while `cProvince_Do` still steps 12×/s. To read the *simulation* rate, use the `usleep` call count — the game issues one frame-limiter sleep per sim step, so `usleep` calls ÷ ~3.5 slices ≈ sim Hz — or set `THEOC_LEGACY_CURSOR=1` to put presents back on the frame. Since 2026-08-04 the sleep column also carries **`(N slices/frame, +M ms each)`** — see "Reading the sleep slices" below, which is the first thing to look at when timing is questioned on a new host. |
| `THEOC_PROFILE` | presence | off | Size-weighted guest basic-block histogram (Σ instruction bytes ≈ work), rolling top-15 dumped every 3s so the window tracks whatever is on screen. Host trap/stub/scratch pages (≥ `0x50000000`) are excluded; addresses are labelled `game 0x…` / `mvos+0x…` for the two Ghidra DBs. Armed just before `Start`, so boot and `.ctors` are not in the sample. Found the hot blit functions. |
| `THEOC_TRACE` | presence | off | 32-entry ring of the last basic-block entries, dumped (oldest-first, labelled) when `Start` faults. Essential exactly when the EBP walk cannot help: at `eip=0` the frame pointer is usually 0 too, and this is then the only thing that shows how control got there. |
| `THEOC_WATCHDOG` | seconds | off; **10s** when the value is ≤ 1 | Host thread, armed on the first present. Polls the present counter every 250ms; after the given stall it samples `exec_blocks` and the trap sequence over 500ms and reports uptime, stall length, guest running/not-running, the last guest EIP, the last trap name and live heap. **Arms the guest block counter itself** — its whole verdict is read off `exec_blocks`, and that counter used to be armed by `THEOC_FPS` alone (see "A counter nobody armed" below). |
| `THEOC_WATCHDOG_SAMPLE` | path | off | **macOS only.** On a **host-side** stall only (guest not executing), shells out to `sample <pid> 1 -file <path>` to capture a native stack of exactly that moment. An aggregate profile over a 40s run cannot isolate a 1.5s window. On Linux and Windows it prints one line saying it is unavailable and points at the stall report's last-trap field plus `THEOC_SLOWLOG` — until 2026-08-04 it instead ran `sample` anyway and printed the shell's failure as a bare `rc=`, which reads as the sampler failing rather than as the feature not existing. |
| `THEOC_SLOWLOG` | milliseconds | off; **250ms** when the value is ≤ 1 | Prints `[slow] <section> took N ms` for any host-side section that blocks the emulation thread past the threshold. Covers every trap dispatch, every plugin dispatch, `OpenDisplay` and `present`. The deliberate frame-cap sleep is credited out, or every capped frame would report as an 83ms "slow" section and bury the real ones. |
| `THEOC_DUMP_WORLD` | presence | off | **What actually ships in a world file.** Four *passive* guest watches (`Machine::add_watch` — the instruction still executes; nothing is patched): `LoadGame` `0x081a07f0` for the path, `CreateMan_fromStream` `0x081becfc` for each man's caste byte, the `cHero` stream ctor `0x080b22f6` for the hero id, and `Item_CreateById` `0x0820d1f0` for the item id **plus its return address** — `0x0820dbd5` means "came out of the world file", anything else is config placement, a mission, or the console. Prints one `[world]` block per file loaded. The man count is deliberate: it is the control that distinguishes "this world has no heroes" from "the watches are not firing", which on the first run of this instrument was the actual answer. Reads the starting world out of the game's own loader instead of re-implementing a load chain ~150 stream constructors deep — [../subsystems/starting-world.md](../subsystems/starting-world.md). |
| `THEOC_WORLD_FILE` | path | off | Serve one chosen file for **every** `init.dat` the guest opens. Which map's world is loaded is otherwise a menu choice the unattended harnesses cannot drive; the redirect is sound because a world file names its own scenario id and `LoadGame` builds the matching `cGameInfo` from it, so the campaign's open can serve `scn3/init.dat` and still load it as scenario 3. Pairs with `THEOC_DUMP_WORLD`; nine headless runs cover the whole tree. Note the `LoadGame(...)` line prints the **guest** path, so it still says `data/campaign/init.dat` — the redirect is logged separately, once per guest path. |
| `THEOC_KEYLOG` | presence | off (first 24 keys only) | Logs every key event for the whole session as `[input] key eKey=0x.. down sc=N quals=0x..`, instead of the 24-event boot budget. The question it answers is the one a broken shortcut always poses: **did the chord reach the guest at all?** A chord that never arrives and a chord that arrives and is ignored look identical on screen and need opposite fixes — a host input bug versus a game gate. `quals` is the SDL modifier state, so a swallowed Alt is visible too. |
| `THEOC_REPORT_CLICKS` | presence | off | Logs every mouse-button-down as `[click] x,y btn= win=WxH screen=0x…`, in a form that pastes straight into a `THEOC_CLICKS` path. The active `cScreen*` (`Intuition+0x24`) doubles as a screen identity — clicks sharing that value are on the same screen. |
| `THEOC_LOUD_ABORT` | presence | off | Default policy is bring-up-friendly: guest `abort()` (the tail of `Fatal()`) logs and returns so the caller continues past non-critical Fatals. Set this and `abort` instead walks the g++ 2.95 EBP chain (max 24 frames, labelled) and `request_stop()`s the current call, so a real fault surfaces at its origin instead of hiding as a silent `OpenSubsystems` restart. |
| `THEOC_ABORT_CAP` | int | `32` | How many ignored aborts before the host gives up and stops. The default policy above returns into guest code that has already decided it cannot continue, so control flow past it is undefined — on Windows that produced an endless `Init` restart loop (see [other-os-ports.md](other-os-ports.md)). Past the cap the host prints the diagnosis and stops rather than spewing. A healthy run aborts **zero** times, so raising this is only for deliberately pushing through a known-benign Fatal. |
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
| `THEOC_LEGACY_SLEEP` | presence | off | Reverts **both** sleep fixes at once: `usleep` goes back to a blind, 100ms-capped sleep with the 30Hz tick and the sound slice serviced only at present (the present-coupled clock that pinned province at 12fps), and with it the re-entrant sleep that delivers ticks *during* a long sleep. Pair it with `THEOC_FRAME_MS=83` to get the pre-2026-08-03 behaviour whole. |
| `THEOC_LEGACY_KEYMB` | presence | off | The cutscene-skip key mailbox. Never posts, so intros become unskippable. A/B switch for input-path hangs. |
| `THEOC_LEGACY_SPRITE` | presence | off | The single-buffer `cSprite::AfterSwapBuffer` patch. Restores the double-buffer slot swap, i.e. the cursor-trail bug on static screens. |
| `THEOC_PROVINCE_MS` | milliseconds | unset = stock **83** (12fps) | Retunes `cProvince_Do`'s frame limiter by rewriting its `0x14585` µs operand. **This is a game-speed control**, not a smoothness one: province steps its simulation once per frame with no wall-clock input anywhere in `cMan::Do`, so `33` gives ~30fps *and* ~2.5× game speed, and `166` gives ~6fps at ~0.5×. The coupling is the engine's design — see [frame-timing.md](frame-timing.md), "Why province stays at 12fps". Range 10–1000 ms; the operand is verified before writing, and a mismatch refuses rather than patches. **`50` (20fps, 1.67×) is the tested sweet spot** — see [frame-timing.md](frame-timing.md). |
| `THEOC_LEGACY_CURSOR` | presence | off | The `cGD_LFB16::Refresh` implementation. Restores the inherited empty flush, so the engine's 30Hz between-frame pointer repaints are discarded and the cursor follows the scene's frame rate (12fps in province). |
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
| `THEOC_FRAME_MS` | milliseconds | **16** (~60fps ceiling) since 2026-08-03; was `83` | Minimum present-to-present interval, applied to every screen. It used to be a *pacer* compensating for our own `usleep` truncating the guest's frame-limiter sleep; now the sleep is honoured in full and province paces itself via `cProvince_Do`'s `Sleep(0x14585 − elapsed)`, so 16ms never fires there. It survives as a **ceiling** because `RealmGameLoop` calls `usleep` zero times — it has no limiter at all, and uncapped it free-ran at ~100fps. `83` restores the old global clamp; `0` is genuinely uncapped. |
| `THEOC_AUDIO_MS` | milliseconds | **120** | Target mixer queue depth, which *is* the audio latency. Too high delays SFX, too low re-introduces underrun stutter. |
| `THEOC_CD_AUDIO` | path | first of `data/cd-uk`, `data/cd-audio`, `data/cd/audio` that has tracks | Directory of **ripped CD audio tracks** — the music, which is Redbook audio and therefore not a file the game ever reads ([music-and-redbook.md](../subsystems/music-and-redbook.md)). Filenames are matched on the **first digit run in the basename, read as the absolute track number on the disc**, so macOS's `2 Audio Track.aiff`, `track02.flac` and `02.wav` all mean track 2. With no rip present every CD ioctl falls back to the historical blanket success and the game behaves exactly as it did before the virtual drive existed — an absent rip is the normal case, since the tracks are copyrighted and out of git. |
| `THEOC_MUSIC_VOL` | 0–100 | **100** | Host-side music level, applied on top of whatever the guest set via `CDROMVOLCTRL`. It exists because **there is no reference for the music-vs-SFX balance**: on real hardware the drive fed the sound card's CD line at a level set outside the game entirely, so nothing in the binaries says how loud music should be against a sword hit. Set by ear. `0` mutes music while still consuming it, so the track still ends when it should and the guest still advances. |
| `THEOC_CD_TRACE` | presence | off | Log every CD ioctl the guest issues — `READTOCHDR`, `SUBCHNL` status/track, stop/pause/resume/volume, and each `idle -> StartTrackForMood` auto-advance. Track *changes* are logged unconditionally (`[cd] play track N`), so this is only needed when the question is why a change did or did not happen. The first thing to reach for when the wrong music plays on a screen: the mood→track table is fixed and known, so a `[cd] play track N` that disagrees with it means the guest chose it, not the host. |
| `THEOC_START_SEC` | seconds | **600**; `0` = unlimited; negative clamps to 0. **`THEOC_LONGRUN` defaults it to `0`** unless set explicitly | Host wall-clock budget for the entire `Start()` call (intros + menu + play). Not an in-game timer. A timeout while the window is still open is reported as "host Start timeout — still in game" and counted as a live session, not a failure — but it *reads* like a fault, so suspect it first whenever a session ends at a suspiciously round elapsed time with nothing wrong in the log. |
| `THEOC_VIDEO_HOLD` | seconds | **2** | How long the window is held open after `Start` returns, so a final frame can be read. Previously visible only inside a sample command line. |
| `THEOC_FIX_SAVE` | path | off | Repairs one `.tsg` save in place and exits without booting — the offline half of the save-corruption fix, and how it is tested (no display, no data tree). See [../subsystems/save-format.md](../subsystems/save-format.md). |
| `THEOC_NO_SAVE_FIX` | presence | off | Stops the host collapsing duplicate province groups when the game closes a save it wrote. The A/B revert for the save fix: with it set, saves grow 4–5 counter units per save again and corrupt at ~51. |
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

### Reading the sleep slices — the host-timing check

**Added 2026-08-04.** The sleep column of `[fps]` reads:

```
sleep 677ms/s in 43 usleep (3.18 slices/frame, +2.78ms each)
```

The guest's frame limiter does not sleep its 83 ms in one call. The `usleep`
handler splits it into slices bounded by the next 30 Hz tick, so a frame is
paced by *several* host sleeps and what matters is how many, and how far each
one overruns what was asked. Both are now counted at the call rather than
derived afterwards:

- **slices/frame** — host sleeps ÷ frames in the window. The frame model wants
  ~3–4. **~1 means the heartbeat has collapsed into the frame rate**, which is
  the exact defect the re-entrant sleep was built to fix, and it is the number
  to watch rather than fps ([frame-timing.md](frame-timing.md)).
- **+N ms each** — elapsed minus requested, averaged over those same sleeps.
  This is the host sleep primitive's **own floor**. It is never zero anywhere
  and a non-zero value is not a fault; the question is always "how does it
  compare to this platform's figure below", and whether slices/frame × overshoot
  is enough to explain a slow frame.

Reference figures, each stated with how it was taken, because they are not
comparable to each other:

| Host | slices/frame | overshoot/slice | How measured |
|---|---|---|---|
| macOS (`usleep`) | 3.00–3.17 | **+0.64 ms** (0.50–0.80) | this instrument, 2026-08-04, interactive province at 11.5 fps via `THEOC_AUTO_PROVINCE=1`, 19 samples |
| Windows (VM, waitable timer) | 3.78 | ~1.0 ms | derived by hand from a 6-min in-game run, 2026-08-04 — [other-os-ports.md](other-os-ports.md) |

**Take this reading in the real configuration or not at all.** The first macOS
measurement was made headless under `SDL_VIDEODRIVER=dummy` and read **+2.1 …
+3.2 ms**, 3–5× the true figure, which briefly made macOS look *worse* than the
Windows VM. The dummy-driver run was sitting at 10.4 fps rather than 12, and the
frame rate being off was the visible tell. Overshoot is a scheduling
measurement: it absorbs whatever else the machine is doing, so a headless or
contended run inflates it and is not comparable to a session anyone plays.

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

### The first line names the build — 2026-08-04

```
=== Theocracy guest-libmvos host v1.0.0 ===
```

`git describe --tags --always --dirty`, resolved at **configure** time and
compiled into `main.cpp` alone. Added for beta testing: a report is only
actionable if we know which binary produced it, and this port ships bundles that
differ in ways a tester cannot see — the 2026-08-04 minimal-ffmpeg swap and the
`WSAStartup` fix both changed behaviour without changing anything on screen.
Putting it in the banner rather than behind a flag means a tester who sends a log
has already answered the question without being asked.

Read it as follows:

- **`v1.0.0`** — a tagged build, reproducible from that tag.
- **`v1.0.0-7-g1a2b3c4`** — seven commits past the tag; `1a2b3c4` is the commit.
- **anything `-dirty`** — built from an uncommitted tree. Both packaging scripts
  print a warning when they produce one, because that is precisely the bundle
  whose bug report cannot be reproduced later.
- **`unknown`** — built with no git and no override. Legitimate for an exported
  tarball; it says `unknown` rather than nothing so a missing version is legible
  as a missing version and not as a changed banner.

`-DTHEOC_VERSION=...` overrides it. Note that this is a **CMake** variable, not
one of the runtime `THEOC_*` environment knobs above — nothing about the version
can be changed after the binary is built, which is the point.

Both bundle directories carry the same string in their name and in their
`README.txt`, so the identity survives a tester unpacking it somewhere and
forgetting where it came from.

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
- `data/commit-log.md` (generate with `tools/dump_commit_log.py`) — the commit
  for each instrument states the problem that
  forced it into existence.
