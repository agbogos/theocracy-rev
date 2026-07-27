# The developer console

How Theocracy's in-game console is built, why it is unreachable in single-player,
and what `THEOC_CONSOLE=1` does about it. Addresses are **Ghidra space**: game
`0x08048000`, libmvos `0x00010000` (libmvos file offset = Ghidra − `0x10000`).

The short version: the console was **never compiled out**. It is fully linked,
constructed on every realm and province screen, and sits behind exactly one
never-taken branch. It is also, as shipped, **half-wired** — even in the
multiplayer battles where the branch *is* taken, a typed command goes nowhere.

## The two consoles

Both are `cVOConsole` objects in the game's `.bss`, adjacent and `0x60` bytes
apart. Both are `Setup` unconditionally by the province screen (`FUN_080bb590`)
and the realm loop (`RealmGameLoop`, `0x81a67a0`).

| | `g_CmdConsole` | `g_LogConsole` |
|---|---|---|
| Address | `0x85c0f80` | `0x85c0fe0` |
| Rect | `{40,460,580,100}` — bottom input strip | `{10,40,460,350}` — big output box |
| `SetExitKey` | eKey `0x13` (**H**), qualifier mask `2` | eKey `0x0e` (**C**), qualifier mask `2` |
| `cShell` attached (`cConsole+0x38`) | **never** | yes — by the game, per screen |
| Opened by | `Edit__10cVOConsole`, gated (below) | auto-shows on output (`Console_ShowAndPrint`, `0x81f3fb0`) |

`cVOConsole` **is-a** `cConsole` (the base sits at offset 0), which is why
`cConsoleVO::Key` can pass the owning `cVOConsole*` straight to `cConsole::Input`.
Useful `cConsole` offsets, all confirmed against libmvos: `+0x38` = `cShell*`
(`cConsole::HaveShell` is literally `this+0x38 != 0`), `+0x3c` = the object's own
vtable pointer, and on the `cVOConsole` side `+0x44` = the `cConsoleVO` widget and
`+0x4c` = link state (`0` unlinked, `1` linked, `2` linked as requester — this is
the game's `DAT_085c0fcc`).

## How you would open it, and where it dies

`InGame_HandleKeyCommand` (`0x81e1aa0`) is the **Alt+key** dispatcher. Its two
callers (`FUN_080bfff0`, `FUN_08125d30`) route a key event by reading the
Intuition key matrix, which is indexed `Intuition + 0x3c + eKey`:

| Matrix bytes | eKey | Modifier | Goes to |
|---|---|---|---|
| `+0x73/0x74` | `0x37/0x38` | Shift | (read as the `shiftMod` field, not a route) |
| `+0x75/0x76` | `0x39/0x3a` | Ctrl | `FUN_081e2120` |
| `+0x77/0x78` | `0x3b/0x3c` | **Alt** | **`InGame_HandleKeyCommand`** |
| — | — | none | `FUN_081e2330` |

The case selector is the raw eKey, and case `0x21` is **V**. So the chord is
**Alt+V** (confirmed live), and this is the whole gate:

```
081e20b0: a1 10 96 4c 08   mov  eax, [0x084c9610]    ; g_GameSession
081e20b5: 80 78 2c 00      cmp  byte [eax+0x2c], 0
081e20b9: 74 1f            jz   0x081e20da           ; <-- taken in SP: return
081e20bb: 68 80 0f 5c 08   push 0x085c0f80           ; g_CmdConsole
081e20c0: e8 ff cf e6 ff   call 0x0804f0c4           ; Edit__10cVOConsole
```

`Edit__10cVOConsole` has **exactly one call site in the entire game binary** —
that one. There is no second way in.

`cVOConsole::Edit` itself only links the console's widget into the screen as a
**requester** (a modal overlay) and sets link state `+0x4c = 2`. It requires
`+0x44` (the `cConsoleVO`) to be non-null, which `Setup` always provides.

## Why `+0x2c` is always 0 in single-player

`g_GameSession+0x2c` is the **multiplayer/battle-mode flag**, not a debug switch.
Scanning the game for accesses through the `g_GameSession` pointer (`0x84c9610`)
gives 65: **62 reads, 4 writes**. The writes are the entire story.

| Site | Value | Function |
|---|---|---|
| `0x814569d` | `0` | `FUN_08145550` — scenario start ("Starting Scenario : %d") |
| `0x81458cd` | `0` | `SetupGame` (`0x81457e0`) — campaign |
| `0x81458e5` | `0` | `SetupGame` again, 4 instructions later |
| `0x829c679` | `1` | `NetGame_InitBattle` (`0x829c630`) |

Two things worth knowing:

- **`GameSession_Construct` never writes `+0x2c` at all.** It initialises `+0x2d`,
  `+0x2e…`, `+0x48`, `+0x4c`, `+0x50` — but leaves the battle flag as whatever
  `operator new` returned. The SP clears are therefore *defensive init against
  uninitialised heap*, not a deliberate "disable the console" decision.
- `SetupGame` clears it **twice**, straddling the `+0x2d` write. Both stores are
  real in the disassembly and both are on the join path every branch falls into.

So the console is not disabled for release. It is live in netgame battles, and
single-player simply never sets the flag it happens to key off.

## The second problem: the command console has no shell

This is the part that is easy to miss, and it makes the obvious patch useless.

On ENTER, `cConsoleVO::Key` case `0x48` calls `cConsole::Input(owner, line)`.
`Input` formats the line into the ring and then dispatches through the object's
own vtable at `+0x3c`, slot `+0xc` — which for both `cConsole` and `cVOConsole`
resolves to **`cConsole::Process`**. And `Process` is null-safe:

```c
cShell *shell = *(cShell **)(this + 0x38);
if (shell != NULL) {
    if (shell[0x40]) Print(this, "> %s", line);
    cShell::Parser(shell, line);          // splits on ' ' -> ProcessCommand(argc, argv)
}
```

`g_CmdConsole+0x38` is **never set**. Both `ChangeShell` call sites in the whole
game push `g_LogConsole` — confirmed at instruction level, since the two sites
attach *different* shells:

| Call site | Screen | Shell | Console |
|---|---|---|---|
| `0x80bb917` | province (`FUN_080bb590`) | `add eax,0x409dc` → province`+0x409dc` | `push 0x85c0fe0` |
| `0x81a6c90` | realm (`RealmGameLoop`) | `add eax,0x5d8` → `g_World+0x5d8` | `push 0x85c0fe0` |

⇒ **The interactive console executes nothing, even in multiplayer.** It opens,
edits a line, and drops it. The design was clearly meant to be "type in the
bottom strip, read output in the big box" — `cShell+0x44` is a back-pointer to
the console the shell reports *to*, and it points at the log console — but the
input half was never connected.

## Why the realm screen has no opener at all

`InGame_HandleKeyCommand` is **not** a global hotkey handler. Key events reach
whichever `cVObject` has focus, through that widget class's **`vtable+0x10`**
slot (4-byte slots; the classes in this family share `+0x14`/`+0x18` PLT entries).
Only two functions in the binary call `InGame_HandleKeyCommand`, and only one of
them — `FUN_080bfff0`, at `vtable+0x10` of the vtable at `0x832c960` — belongs to
the **province** view's widget.

The realm map view is a different class (its object is built by `FUN_081a7180`
in `RealmGameLoop`), and its handler never routes Alt+key anywhere near case
`0x21`. `RealmGameLoop`'s own event drain (`g_RealmScreen+0x70..0x7c`) does not
dispatch at all — it only `delete`s events of type `-1`.

⇒ On the realm screen there is **no branch to patch, at any address**. Which is
why the whole approach had to change.

## What `THEOC_CONSOLE=1` does

**It does not patch the game.** It calls the opener itself:

```
Alt+V (SDL event hook)  ->  console_open_pending_
next present            ->  guest call Edit__10cVOConsole(g_LogConsole)
```

`TrapLayer::enable_dev_console` arms it; `TrapLayer::maybe_redirect_console`
services it from the present path, rewriting the trap return into a cdecl guest
call the same way the timer and sound slices do (a nested `uc_emu_start` crashes
Unicorn). Being screen-independent, it works on realm and province alike, and it
needs no patch site, no opcode signature and no `g_GameSession+0x2c` games.

**Why `g_LogConsole` and not `g_CmdConsole`** (the strip the shipped call opens):
the command console is a dead end on both halves — no shell at `+0x38` to execute
with, and it is not the shell's print target either. `g_LogConsole` is both, so
input, echo and output land in one visible box.

**Guard:** the open is refused unless `g_LogConsole+0x38` (the shell) is non-null.
`ChangeShell` runs at realm/province entry and `RestoreShell` at exit, so that
field is set exactly while a game screen is live; outside that window the
`cConsoleVO` at `+0x44` is stale from a previous screen and `Edit` would link a
dead widget. Alt+V elsewhere logs and does nothing.

The `V` keypress is swallowed (down and up) exactly as `Alt+Enter` already is —
eKey `0x21` is a live game key, and leaking it would also type a stray `v` into
the console being opened.

`g_LogConsole` (`0x85c0fe0`) is the only game address left in the host for this
feature. It is not a dynamic symbol — `theocracy.real` is `.symtab`-stripped and
none of its 348 exports covers it — so it cannot be resolved by name the way
`guestlink::abs_sym` does for the boot path.

### The host bug this uncovered

`vsprintf` was an **unimplemented trap**. It is the first call in
`cConsole::Input`, so every command formatted into a buffer the host never wrote,
and nothing downstream could work — with or without any patch. It is the only
printf-family symbol libmvos imports that the host lacked (`printf`, `fprintf`,
`sprintf` were all present). Implemented in `register_builtins`: on i386 a
`va_list` is just a pointer into the caller's stack, so it reuses `format()` with
base `ap - 4`.

## Using it — the commands

The shell is real and has a help system. **`help`** or **`?`** prints the command
list for the screen you are on; several commands print their own syntax when
called with no arguments. The shells are per-screen and reject each other's
commands ("On realm screen you have different commands!").

`cShell` carries its name at `+0x00` (hence the `Shell Changed to Province Shell`
log line), an echo flag at `+0x40` — set to **1** by both ctors, so input is
echoed back as `> <line>` — and its vtable pointer at `+0x4c`, which is what
`cShell::Parser` dispatches through (`vt+0xc` = `ProcessCommand`).

| Shell | Ctor | vtable | `ProcessCommand` |
|---|---|---|---|
| Realm Shell | `0x81f33cb` | `0x8391954` | `0x81f3410` |
| Province Shell | `0x81eed4b` | `0x838f4b4` | `0x81eed90` |
| `cChatConsole` (netgame chat) | `0x81f3f13` | `0x83923a0` | `0x81f3e30` |

**Realm screen:** `edit`, `save`, `tribe <0-7>`, `owner <0-7>`, `jewel <+/-value>`,
`date <year> <month> <day>`, `mannaking <mana> [sphere]`, `missionstat`, `aiprov`,
`set-def-price`, `ghost`, `prof`, `hello` (→ `Szia.`), `bye` / `exit` / `quit`.
`save` writes `%s/init.dat` and is edit-mode only.

**Province screen:** `mitem`, `allspell`, `myspell`, `mannaking`, `getdump`,
`setdump`, `clrdump`, `allcheat`, `onlycheat`, `printid`, `language`, `teleport`,
`dragon`, `allbuilding`, `punt`, `guard`, `prof`, `stat`, `maninfo`, `mapbitmap`,
`mapfill`, `killtribe`, `edit`, `man`, `building`, `missionflag`, `values`,
`move`, `destroy`, `delbld`, `resource`, `hero`, `anim`, `clearanim`, `saveanim`,
`pos`.

`mannaking` takes `<mana> [sphere]` with spheres `sun, moon, stars, nature, soul`.
`allcheat` / `onlycheat` toggle `RealmCheat` and `ProvCheat`.

Note that shell **output** goes to the console the shell's `+0x44` back-pointer
names — the *log* console (the big box), not the input strip you type into. That
is the intended split, and it is why the input strip can look inert.

## Status

**Working on both screens, verified interactively (2026-07-27).** Alt+V opens,
Alt+C closes, and the command sets respond on realm and province.

- **The qualifier mask is resolved.** `SetExitKey` stores the key at
  `cConsoleVO+0xb8` and a mask at `+0xbc`; `cConsoleVO::Key` closes when
  `code == exitKey && (quals & mask) == mask`. Bit 0 is Shift (the only bit
  `cKeyboard::RawkeyToAscii` reads — it selects between two 100-entry tables) and
  **bit 1 is Alt**, established by Alt+H closing the command console (exit key
  `0x13` = H, mask 2). So `g_LogConsole`'s `(0x0e, 2)` is **Alt+C**.
- **The red console is not a bug.** It renders with `data/fonts/small_red.mft` —
  the console's own font, visible in the log as `SinglePalette font [...]`. An
  earlier draft of this doc filed it as an open cosmetic defect; it never was one.

## Cross-references

- [game-loop-and-simulation.md](game-loop-and-simulation.md) — `RealmGameLoop`,
  which is where the realm-screen console and its shell are set up.
- [multiplayer-and-factions.md](multiplayer-and-factions.md) — `+0x2c` as the
  battle-mode flag and everything else it switches.
- [../structs/cGameSession.md](../structs/cGameSession.md) — the `0x58` layout.
- [../porting/diagnostics.md](../porting/diagnostics.md) — the `THEOC_CONSOLE` row.
