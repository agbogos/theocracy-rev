# Resume note — Windows port, 2026-08-03

**Ephemeral. Delete this file when the Windows port is either working or
abandoned** — the durable knowledge belongs in `docs/`, and this repo
deliberately has no worklist file (`task_fifo.md` was retired 2026-08-03; do not
let this become its replacement).

## Where things stand

macOS: playable. Linux: done and confirmed by play. **Windows: builds, boots,
and has never got past `Start`.**

Everything below is written up properly in
[`docs/porting/other-os-ports.md`](docs/porting/other-os-ports.md) — read "The
Windows build" and the two sections after it. This file is only the *state*, not
the findings.

Last session: cross-toolchain set up, `traps.cpp` ported to Winsock2, bundle
built, first Windows run attempted. It boots identically to macOS all the way
into `Start`, then hit a bug that turned out to be **latent on every platform**
(the single-instance lock faked `bind` without binding; POSIX `listen()` forgave
it by auto-binding, Winsock did not). Fixed and repackaged, **not retested**.

## Next two steps, in order

### 1. USER: retest `dist/theoc-windows-x64/` on the Windows VM

Already built and waiting — it contains the lock fix. Nothing to rebuild first.
Run `theoc.bat`. Expect it to get past "You can run only one Theocracy in the
same time!" and further into `Start`.

**Expect province to run slow** (~13%) if it renders. That is step 2, not a new
bug.

Ask for the output. If it fails, the prime suspect is **path handling**:
`resolve_path` tests `guest[0] == '/'` for "is absolute", which is not how
Windows spells that.

### 2. ME: implement `sleep_us()` on `CreateWaitableTimerEx`

The probe already ran and decided this — **do not re-measure, and do not
re-litigate the design.** Results are in other-os-ports.md, "The probe's answer".
The short version:

- naive `Sleep()` → 15.9 ms for a 0.1 ms request; province frame 94 ms vs 83.3
- `CreateWaitableTimerEx(HIGH_RESOLUTION)` → 0.63 ms; province frame 84.0 ms
- **`timeBeginPeriod(1)` adds nothing on top of the timer** — measured. Do not
  add it; raising system-wide timer resolution is a global side effect with a
  power cost, for no gain.

Implementation shape, consistent with what is already in the file:

- Add `theoc_sleep_us()` next to `theoc_mkdir()` in the platform block near the
  top of `traps.cpp` (~line 700). Keep it a narrow `#if defined(_WIN32)` helper.
  There is still no `port/src/platform/` directory and it has not earned one —
  the Winsock work stayed inline too.
- Windows: **one** timer handle created once and reused (creating one per sleep
  is a syscall per slice at ~40 slices/s). `SetWaitableTimer` with a negative
  relative due time in 100 ns units, then `WaitForSingleObject`.
- Fall back to `timeBeginPeriod(1)` + `Sleep()` if `CreateWaitableTimerEx`
  rejects `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (pre-Windows-10-1803). The
  probe reports which one it got; mirror that.
- POSIX: `::usleep(us)`, unchanged.

**Three call sites**, all in `traps.cpp`:

| Line | Context |
|---|---|
| ~1402 | `THEOC_LEGACY_SLEEP` blind-sleep A/B path |
| ~1473 | the real one — the tick-bounded slice loop |
| ~4349 | the `THEOC_FRAME_MS` present-to-present cap |

Then rebuild both platforms and re-run `tools/package-windows.sh`.

## Things that cost time last session

- **The shell is zsh with `noclobber`.** `>` on an existing file silently fails
  and you read a stale log and misdiagnose. Use `>|`. This bit me twice.
- **Don't relaunch the game** — the user drives runs (see CLAUDE.md). Ask for
  output; absence of a log line is evidence.
- `pkg-config` is required by Unicorn's bundled `qemu/configure`, which runs
  under `execute_process` with **no error checking**, so its failure surfaces
  ~200 files later as a missing `config-target.h`. Already installed now.
- The macOS clang LSP reports dozens of errors on `traps.cpp` because it cannot
  find `unicorn/unicorn.h` / `windows.h`. **Ignore them**; trust the actual
  builds.
- Ghidra was never needed for any of this. If it becomes needed, ask the user
  which binary is loaded — the MCP shows one at a time.

## Build commands

```sh
cmake --build port/build                       # macOS
sh tools/package-windows.sh                    # Windows -> dist/theoc-windows-x64/
                                               # (deps already staged in port/deps-win/)
```

Both `port/deps-win/` and `dist/` are gitignored — third-party and built
artifacts never enter git.
