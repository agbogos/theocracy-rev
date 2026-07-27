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
gives 65: **61 reads, 4 writes**. The writes are the entire story.

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
list for the screen you are on. The two shells are separate and reject each
other's commands ("On realm screen you have different commands!"). An
unrecognised word prints **`Unknown command.`** — which is itself useful: silence
means something is broken upstream, not that you mistyped.

`cShell` carries its name at `+0x00` (hence `Shell Changed to Province Shell` in
the log), an echo flag at `+0x40` — set to **1** by both ctors, so input is echoed
as `> <line>` — and its vtable at `+0x4c`, which `cShell::Parser` dispatches
through (`vt+0xc` = `ProcessCommand`).

| Shell | Ctor | vtable | `ProcessCommand` |
|---|---|---|---|
| Realm Shell | `0x81f33cb` | `0x8391954` | `0x81f3410` |
| Province Shell | `0x81eed4b` | `0x838f4b4` | `0x81eed90` |
| `cChatConsole` (netgame chat) | `0x81f3f13` | `0x83923a0` | `0x81f3e30` |

### Realm screen

Its `help` is the better of the two — it is *categorised*, which a flat string
dump hides:

```
You can use the following developer commands:
missionstat, ghost, prof, aiprov.

You can use the editor commands:
edit, save, tribe, owner, jewel, date, mannaking, mem.

On province screen you have different commands!
```

| Command | Advertised | Syntax / behaviour |
|---|---|---|
| `help`, `?` | — | prints the block above |
| `missionstat` | dev | `missionstat [n]` — one mission, or all if no argument |
| `ghost` | dev | no arguments |
| `prof` | dev | profiler dump (`FUN_08072ca0`) |
| `aiprov` | dev | hover a province first; prints `TRIBE / CAPITAL DISTANCE / OPTIMAL FORCE / ACTUAL FORCE`. With no province hovered it prints its own 3-line description |
| `edit` | editor | **reports** edit mode, does not toggle it — see below |
| `save` | editor | writes `<scenario>/init.dat`; refuses with `Only in edit mode.` |
| `tribe` | editor | `tribe <num(0-7)>` — sets `g_GameSession+0x2d`, the local faction |
| `owner` | editor | `owner <num(0-7)>` — reassigns the hovered province |
| `jewel` | editor | `jewel <+/-value>` |
| `date` | editor | `date <year> <month> <day>` |
| `mannaking` | editor | `mannaking <mana> [sphere]`, spheres `sun, moon, stars, nature, soul`; no args prints syntax, a description and the current five values |
| `mem` | editor | **advertised but has no handler** (see below) |
| `hello` | *hidden* | replies `Szia.` |
| `bye`, `exit`, `quit` | *hidden* | `exit(0)` — quits the process immediately, no save prompt |
| `set-def-price` | *hidden* | resets default prices across every province |

### Province screen

Its `help` is a flat list with no categories:

```
mitem, allspell, myspell, mannaking, getdump, setdump, clrdump, allcheat,
onlycheat, printid, language, teleport, dragon, allbuilding, punt, guard,
prof, stat, maninfo, mapbitmap, mapfill, killtribe, edit, man, building,
missionflag, values, move, destroy, delbld, resource, hero, anim, clearanim,
saveanim, pos
```

36 advertised. Of these, `getdump`, `setdump` and `clrdump` **have no handler**
(below). Only three carry any description of their own: `stat` (its own
`Available commands:` block — `(nothing)` prints all statistics, `help`, `clear`),
`mannaking` (as on realm), and `printid` (prints `Registration ID:[%s]`). The
whole province handler contains exactly **one** `Syntax :` string, against the
realm shell's five.

### Undocumented literals

`ProcessCommand` for the province shell compares against **57** distinct string
literals, versus the 36 its help advertises. The extra ones are real — they are
in the code — but they are a **mix of top-level commands and sub-arguments of
other commands**, and the two cannot be reliably separated from the decompiler
output, because it reuses variable names across the else-if ladder and the argv
load is hoisted far from each comparison. Two independent classification passes
disagreed, so no split is claimed here.

- **Confirmed top-level and hidden** (read directly from the code, sitting in the
  same ladder as `onlycheat`/`printid`): **`zila`**, **`bagoy`**, **`tomy`** —
  Hungarian, and almost certainly developer nicknames used as personal shortcuts.
- **Clearly sub-arguments** by form and by the commands they neighbour: `on`,
  `off`, `enable`, `disable`, `add`, `sub`, `set`, `all`, `closest`, `null`,
  `alfa`, `clear`.
- **Unclassified** — present in the handler, absent from help, could be either:
  `kill`, `capture`, `goto`, `fillpeople`, `canbuild`, `buildpercent`, `mapanim`,
  `win`.

Resolving the split properly needs the disassembly (tracking which argv slot each
comparison's source register was loaded from), not the decompile — see
[../reference/re-methodology.md](../reference/re-methodology.md) §5.

### Commands advertised with no handler

Four strings appear **only inside the help literal** and are never compared
against in `ProcessCommand`:

| Shell | Command |
|---|---|
| Realm | `mem` |
| Province | `getdump`, `setdump`, `clrdump` |

Typing them yields `Unknown command.` They are the residue of a help string that
outlived its implementation — worth knowing before hunting for a handler that
does not exist.

## Edit mode

**Edit mode is `g_GameSession+0x50`** — the byte
[../structs/cGameSession.md](../structs/cGameSession.md) currently calls
`bPaused`. That name is at best half right: it *does* gate the simulation, but it
is set once at load and is the editor switch, not a pause control.

Written in exactly two places, both at session construction:

| Site | Source of the value |
|---|---|
| `GameSession_Construct(session, scenarioID, startPaused)` (`0x817af70`) | `+0x50 = param_3` |
| `FUN_0817b610` — the load-game session ctor | `+0x50 = param_3`, passed down from `FUN_081a07f0(path, editFlag)` = **LoadGame** |

`SetupGame(1)` → `LoadGame(…, 1)` = edit mode; `SetupGame(2)` → `LoadGame(…, 0)`
= normal. The printfs name them: *"Scenario edit mode"* / *"Scenario normal
mode"*.

**What it does.** A byte-level scan of accesses through the `g_GameSession`
pointer finds **58 reads and zero writes** — so it is decided at load and never
toggles at runtime. Two effects are pinned down:

- `RealmGameLoop`: `if (g_GameSession+0x50 == 0) SimulationUpdate(g_World)` —
  **edit mode freezes the simulation**.
- The console `save` command refuses unless it is set; `edit` merely *reports* it
  (`You are in edit mode now.` / `You are not in edit mode.`) and cannot change it.

The other 63 read sites are not individually characterised.

**It is unreachable as shipped.** Both `SetupGame` call sites push `2`, and the
scenario-start path (`FUN_08145550`) is called with `(2, 0)`. Nothing in the
shipped menus selects mode 1.

**Enabling it — `THEOC_EDIT=1`.** Because the flag is read live everywhere and
nothing writes it back, the host simply stamps it: `TrapLayer::apply_edit_mode`
reads `g_GameSession` and sets `+0x50 = 1`. No patching.

Applied per present rather than once, because the game builds a **new**
`cGameSession` on every scenario load and re-initialises `+0x50` from `LoadGame`'s
`editFlag` — a one-shot stamp would survive only until the next load. It logs
once per session:

```
[edit] edit mode on for session 0x6020b030 (sim frozen; console `save` now allowed)
```

Note this necessarily **freezes the simulation** — edit mode *is* the frozen-sim
state, not a cheat layered on normal play. Verified by driving into a scenario
with `THEOC_AUTO_PROVINCE=1`.

## Cheats

Three bytes in `.data`, all zero-initialised:

| Address | Name (from its own log strings) | Read by |
|---|---|---|
| `0x84c9123` | **RealmCheat** | realm-view code — `0x81a97a0`, `0x81a9821`, `0x81a9b4f` |
| `0x84c9124` | **ProvCheat** | `0x81b5be8`, and all three key dispatchers: `0x81e1bf0` (inside `InGame_HandleKeyCommand`, gating case `0x1c` = **Alt+Q** → `FUN_081d81d0`), `0x81e22d2` (Ctrl), `0x81e2366` (plain) |
| `0x84c9125` | *(unnamed)* | toggled by `onlycheat`; readers not characterised |

```c
// onlycheat
if (DAT_084c9123 == 0) { DAT_084c9123 = 1; Print("RealmCheat Enabled"); }
if (DAT_084c9124 == 0) { DAT_084c9124 = 1; Print("ProvCheat Enabled");  }
DAT_084c9125 = DAT_084c9125 ^ 1;
```

```c
// allcheat  — toggles BOTH, then prints the resulting state
al = DAT_084c9123; al ^= 1; DAT_084c9123 = al;   // 0x81ef5f7..0x81ef5ff
DAT_084c9124 ^= 1;                               // 0x81ef605
Print(al ? "RealmCheat Enabled" : "RealmCheat Disabled");
Print(DAT_084c9124 ? "ProvCheat Enabled" : "ProvCheat Disabled");
```

- **`allcheat` toggles both flags**; **`onlycheat` force-enables both** and
  additionally toggles `0x84c9125`. The `… Disabled` strings report the state
  *after* the toggle.
- So cheats **can** be turned back off: run `allcheat` twice.
- The effect is to **unlock extra in-game key commands** — the ProvCheat readers
  are the key dispatchers. Decoded below.

> **Corrected 2026-07-27.** This section previously said `allcheat` merely
> reported status and that "there is no write of `0` to either flag anywhere in
> the binary". Both were wrong. The toggle is a `xor` — `mov al,[flag]; xor al,1;
> mov [flag],al` on RealmCheat and `xor byte [flag],1` on ProvCheat — and the
> ad-hoc scan behind the original claim did not decode `88 /r` (store) or the
> `80 /6` (xor) group, so a read-modify-write read as "never written".
> `tools/elfq.py xref-global` covers the whole `80 /n` group precisely because of
> this.

### What the cheat keys actually do

`ProvCheat` gates three separate things. The big one is in the **plain-key**
dispatcher (`FUN_081e2330`, i.e. no Ctrl and no Alt held) at `0x81e2366`:

```
81e2366: cmp byte [ProvCheat], 0
81e236d: jz   0x81e65d6              ; cheats off -> ignore the key
81e2373: mov  ecx,[ebp+0x14]         ; eKey
81e2376: add  ecx,-2
81e2379: cmp  ecx,0x57
81e237c: ja   0x81e65d6
81e2382: jmp  [0x838ab18 + ecx*4]    ; 88-entry jump table
```

So with cheats on, **bare keypresses in the province view** run cheat handlers.
The table at `0x838ab18` covers eKey `0x02..0x59`; **42 of its 88 entries are
live**, the rest go to the common exit. Identified by the strings they print:

| Key | Effect |
|---|---|
| `V` | `Real visibility mode turned ON./OFF.` — fog of war |
| `X` | `Surrender mode turned ON.  This is the real mode.` / `OFF.  This is the cheat mode.` |
| `Z` | `Men tire while do something` / `Men don't tire while do something` — stamina |
| `G` | `Governor is removed from province` |
| `B` | `Only for test!!!` |
| `F9` | `Swordsmen created` |
| `F10` | `Swordsmen+Spearmen created` |
| `F11` | `Swordsmen+Archers created` |
| `F12` | `Spearmen+Archers created` |
| `F8` | toggles `0x84c9fe6` and printfs the new state |
| `F4` | touches `0x84c9125` (the third cheat byte) and calls `FUN_081d0e20`, the same helper the Alt dispatcher's quick path uses |

Two families were left undecoded, both uniform enough to characterise without
reading each one:

- **`1`–`8` and `A`** — nine 16-byte stubs at `0x81e4210`, each
  `mov word [0x84ca13e], N` for `N` = 0..8 then jump to the common exit. A
  16-bit selector; what it selects is not chased.
- **The letter cluster** (`S D F H J K L Q W E R T Y U I`) — near-identical
  handlers ~`0x180` bytes apart, all opening on the world's unit/selection
  arrays (`+0x40b30`, `+0x40dc0`, `+0x40dc4`). Same shape as the `F9`–`F12`
  spawners, and laid out as a keyboard cluster, so almost certainly per-type
  unit creation. Not individually confirmed.

The other two gates are small:

- **Ctrl dispatcher** (`0x81e22d2`): exactly one key — `cmp [ebp+0x14], 0x25` =
  **Ctrl+Z**. Guarded on a further global `0x84c9ff8` being non-null, then sets
  `[world+0x40dd4] = 6` and calls `FUN_0814e2b0`.
- **Alt dispatcher**: `InGame_HandleKeyCommand` case `0x1c` = **Alt+Q** →
  `FUN_081d81d0`.
- **`0x81b5be8`**: a permission check that is bypassed when ProvCheat is on **and
  Right-Shift is held** (`Intuition+0x3c+0x38`), skipping a test of `[obj+0x10]`.

**Two orphaned enablers.** `FUN_080635a0` and `FUN_080635d0` both set
`Intuition_Mode` (to `1` and `-1`) and turn **both** cheat flags on. They sit
immediately after `RollingDemoFrame__Fv` (`0x8063540`), i.e. in the attract/demo
-mode neighbourhood, which fits their setting of `Intuition_Mode`. Neither has a
single reference anywhere — not a call, not a data word in any table (checked
across `.rodata` and `.data`; the only hit is an `.eh_frame` entry, which is not a
reference — see [../reference/re-methodology.md](../reference/re-methodology.md)
§3). They are dead entry points, presumably a stripped command-line or debug-menu
hook. In the shipped binary the console is the only way to turn cheats on, which
is circular: you need the console, which needs multiplayer battle mode.

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

### Open — and deliberately left so

The cheat/console surface below is **incomplete by choice**, not by oversight.
The console itself works on both screens and the useful cheats are identified;
the remainder is tangential to the port, so it is **not tracked in
`../../task_fifo.md`** and nobody should expect it to be picked up in order.
Recorded here so the next person knows where the edge is rather than
rediscovering it.

- **`RealmCheat` is entirely unexamined.** Three read sites — `0x81a97a0`,
  `0x81a9821`, `0x81a9b4f`, all in realm-view code. The flag and its toggles are
  understood; nothing about what it *unlocks* has been looked at.
- **ProvCheat leftovers.** The `0x84ca13e` selector (keys `1`–`8`/`A` write 0..8
  into it) — meaning unknown. The ~15 letter-cluster handlers are characterised
  as a family but not read individually; reading all of them is the arcane part
  and was explicitly stopped.
- **`0x84c9125`.** The third cheat byte `onlycheat` toggles; readers not traced.
- **Province command classification.** 57 literals vs 36 advertised, with no
  reliable split between top-level commands and sub-arguments. Needs disassembly,
  not decompile.

## Cross-references

- [game-loop-and-simulation.md](game-loop-and-simulation.md) — `RealmGameLoop`,
  which is where the realm-screen console and its shell are set up.
- [multiplayer-and-factions.md](multiplayer-and-factions.md) — `+0x2c` as the
  battle-mode flag and everything else it switches.
- [../structs/cGameSession.md](../structs/cGameSession.md) — the `0x58` layout.
- [../porting/diagnostics.md](../porting/diagnostics.md) — the `THEOC_CONSOLE` row.
