# Resume note — Windows port, 2026-08-04

**Ephemeral. Delete this file when the Windows port is either working or
abandoned** — the durable knowledge belongs in `docs/`, and this repo
deliberately has no worklist file (`task_fifo.md` was retired 2026-08-03; do not
let this become its replacement).

## Where things stand

macOS: playable. Linux: done and confirmed by play. **Windows: runs.** Past
`Start`, into the game.

Both of the previous session's two steps are done. The single-instance lock fix
was confirmed by the user's run, and `theoc_sleep_us()` on
`CreateWaitableTimerEx(HIGH_RESOLUTION)` is implemented, built and packaged —
`dist/theoc-windows-x64/` is current and contains it.

The findings are written up in
[`docs/porting/other-os-ports.md`](docs/porting/other-os-ports.md): "The probe's
answer" now ends with a "What shipped" section, and "The game runs on Windows"
replaces the old first-run narrative's open ends. This file is only the *state*.

## What is left before this file can be deleted

**A proper playtest, with comparison against the old screenshots** — the
standard Linux was held to ("Confirmed by play"), not "it runs and looks right".
That is the user's, and it is the only thing standing between here and done.

Worth exercising while playing, in rough priority:

1. **Province frame rate.** The timer fix predicts ~84 ms/frame where the naive
   build measured 94. `THEOC_FPS=1` prints it. If province still runs visibly
   slow, that is the one regression this session could have caused.
2. **Save/load.** The `O_BINARY` work is untested by a real save — CRLF
   translation would corrupt `.tsg` silently.
3. **Multiplayer.** Never tried on Windows, and Winsock is the largest
   rewritten surface in the port.

Then: delete this file, and fold anything left into `docs/`.

## Things that cost time in earlier sessions

- **The shell is zsh with `noclobber`.** `>` on an existing file silently fails
  and you read a stale log and misdiagnose. Use `>|`.
- **Don't relaunch the game** — the user drives runs (see CLAUDE.md). Ask for
  output; absence of a log line is evidence.
- `pkg-config` is required by Unicorn's bundled `qemu/configure`, which runs
  under `execute_process` with **no error checking**, so its failure surfaces
  ~200 files later as a missing `config-target.h`. Already installed now.
- The macOS clang LSP reports dozens of errors on `traps.cpp` because it cannot
  find `unicorn/unicorn.h` / `windows.h`. **Ignore them**; trust the actual
  builds.
- Ghidra has not been needed for any of the Windows work. If it becomes needed,
  ask the user which binary is loaded — the MCP shows one at a time.

## Build commands

```sh
cmake --build port/build                       # macOS
sh tools/package-windows.sh                    # Windows -> dist/theoc-windows-x64/
                                               # (deps already staged in port/deps-win/)
```

Both `port/deps-win/` and `dist/` are gitignored — third-party and built
artifacts never enter git.
