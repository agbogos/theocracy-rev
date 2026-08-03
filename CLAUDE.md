# Working in this repo

Theocracy (Philos Laboratories, 2000) running natively on macOS: both
`theocracy.real` and the real `libmvos.so` execute as original i386 code under
Unicorn, and only the OS/library ABI is emulated by the host in `port/`. The
port is one goal; the reverse-engineering write-up in `docs/` is the other.

## Orientation, in order

1. `docs/README.md` — the index. Every doc is one line there, grouped by state,
   and its status table says where the project actually stands.
2. `docs/porting/host-architecture.md` before touching `port/src`;
   `docs/porting/diagnostics.md` before debugging anything.

There is **no worklist file**. `task_fifo.md` was retired on 2026-08-03 when the
last item closed; its history is in git and everything it knew was moved into
`docs/`. What is next is `docs/porting/native-rewrite.md` (retire Unicorn
gradually) and `docs/porting/other-os-ports.md`. What is still *unknown* is
`docs/open_questions.md` — stable IDs, cited from outside, never renumbered.

Findings live in `docs/`, not in commit messages. If a finding has no doc that
owns it, that means a doc is missing.

## Ghidra — ask, don't infer

**Both binaries are loaded in Ghidra and reachable over MCP.** Use it. Do not
infer a fact about the binaries from a decompile you remember, a doc, or a
guess when you can read it.

- The MCP shows **one program at a time**, and the user switches it manually —
  so **ask which one is open** before a session of binary work, and ask them to
  switch when you need the other.
- The tool schemas are deferred: load them with
  `ToolSearch("select:mcp__ghidra__decompile_function,…")` before calling.
- `get_function_by_address` matches **entry points only**; a mid-body address
  answering "no function at address" means nothing.

Before writing an address or offset into a doc or into code, check it against
`docs/reference/re-methodology.md` — it lists the exact ways this project has
got them wrong before.

## Conventions

- **Addresses are Ghidra-space** unless labelled otherwise: libmvos base
  `0x10000` (file offset = Ghidra − `0x10000`), game base `0x08048000`. The
  host's logs and `include/mvos_api.hpp` use libmvos **file** offsets. Always
  say which space you mean.
- **Symbols are GNU v2 mangled** and modern `c++filt` silently echoes them back
  unchanged. Use `tools/gnuv2_demangle.py` or look them up in
  `data/mvos_exports.tsv`.
- **Commit straight to `main`.** No feature branches — solo, local, linear.
- **Docs ship in the same commit as the code.** Keep the *commit message*
  short: what changed and why, a few lines. The depth belongs in `docs/`, which
  is the whole point of having it.
- **Write only inside this repo** — not `~/`, not a tool's conventional home
  elsewhere. Ghidra scripts go in `tools/ghidra/`.
- The shell is **zsh with `noclobber`** — use `>|` to overwrite a file.

## Running it

Needs the copyrighted binaries in `data/cd/` and the extracted data in
`data/game/`, neither of which is in git, plus a display. Don't assume a run
will work headless.

```sh
cmake --build port/build
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./port/build/theoc
```

Every runtime knob is `THEOC_*` and catalogued in
`docs/porting/diagnostics.md`. On any "it froze", reach for `THEOC_WATCHDOG=1`
first: it says whether the guest is spinning (with the EIP) or the host is
wedged (with the last trap).
