# `reconf` — Philos' post-release reconfiguration tool

A 78 KB i386 ELF that is **not on the CD**, found in 2026-08-15 on a
[holarse.de](https://files.holarse.de/native/Spiele/Theocracy/) mirror of the
long-dead dlh.net download, described there as an official Philos-released
reconfiguration script. It is not a script: it is a small unstripped C++ console
program that rewrites `mvos.cfg` by asking five questions.

It matters to this repo for one concrete reason: `data/game/mvos.cfg` is
hand-authored by us because the installer normally writes it, and this is the
only surviving Philos code that writes that file.

## The artifact

| | |
|---|---|
| Archive | `theocracy_reconf.tgz`, 31226 bytes, SHA-256 `c75d78c704da29b3c732334133a1d1d0d21df5ce71ff716b3ce11d23d06619f3` |
| SHA-256 | `9c3bd6e1cde014d26685d940932acf9c740bf17873b28accc1aa10a17221f11b` |
| MD5 | `552bb4a4aa62f818a967d1a6d05de0b5` |
| Size | 78351 bytes |
| Type | ELF 32-bit LSB executable, i386, dynamically linked, `/lib/ld-linux.so.2`, for GNU/Linux 2.0.0, **not stripped** |
| Ghidra base | `0x08048000` (ET_EXEC — Ghidra address *is* the runtime address) |
| Source file | `reconf.cpp`, per `.strtab` |
| Compiler | `GCC: (GNU) 2.95.2 20000116 (Debian GNU/Linux)` |
| **Binary mtime** | **2000-09-21 10:11:37 UTC**, mode `0755`, owner `blkhfc` (uid/gid 1036) |
| **Packed** | 2000-09-21 11:29:01 UTC (gzip header), 78 minutes after the binary |

Not in git — copyrighted like the rest of the distribution, and covered by
`.gitignore`. Supply it out of band.

The tarball carries one member and its SHA-256 matches the loose binary exactly,
so the extracted copy is faithful. Keep the **archive**, not just the binary:
the extracted file's mtime is only the extraction date, and the timestamps above
are the whole dating argument.

`blkhfc` is presumably a Philos developer's account on the machine that built or
packed it. It appears nowhere else in this project.

## Is it really post-release?

Yes — **by seven months, and it is dated rather than argued.** The tarball's
stored timestamps put the binary at **2000-09-21**, against a CD mastered 23–25
Feb 2000.

The rest of this section is the case as it stood *before* the archive turned up,
kept because it was built independently and every piece of it agrees with the
answer. That is worth something: the same reasoning is all that will be
available for the next artifact that arrives without its container.

The website's claim was not self-evidently true, and this project has been
burned before by a plausible attribution hardening into a documented fact
([re-methodology.md](re-methodology.md) §12). So it was tested rather than
repeated. Four pieces of evidence, in descending strength:

1. **The installer's own sentence was rewritten around it.** `inst.linux`, which
   shipped on the CD, contains `To reconfigure please edit %s, or %s!` — two
   paths, hand-editing. `reconf` contains the same sentence updated for a tool
   that now exists: `To reconfigure please run reconf!`. That is the shipped
   advice being superseded.
2. **Nothing on the CD knows about it.** A `grep -ril reconf` over the whole
   `data/cd` tree returns nothing: not `readme.linux` (which is otherwise a
   detailed troubleshooting document, exactly where such a tool would be
   mentioned), not `theocracy.real`, `libmvos.so.0.9`, `server`, or `inst.linux`.
3. **It is a sibling of the installer, not a third-party reimplementation.** It
   statically links the same in-house classes (`cString`, `cConfig`, `cList`,
   `cNode`) from the same source files (`strings.cpp`, `strings.hpp`,
   `list.cpp`), and it shares the installer's compiler *family* rather than the
   game's.
4. **It was built with a newer compiler package than the installer.**

| Binary | `.comment` | CD mtime |
|---|---|---|
| `theocracy.real` | egcs-2.91.60 Debian 2.1 (egcs-1.1.1) | 25 Feb 2000 |
| `libmvos.so.0.9` | egcs-2.91.60 Debian 2.1 (egcs-1.1.1) | 25 Feb 2000 |
| `server` | egcs-2.91.60 Debian 2.1 (egcs-1.1.1) | 25 Feb 2000 |
| `inst.linux` | **gcc 2.95.2 19991109** | 23 Feb 2000 |
| **`reconf`** | **gcc 2.95.2 20000116** | *(lost)* |

The game and its engine were built with the older egcs; the *tooling* around
them with 2.95.2. `reconf`'s package is ~2 months newer than the installer's.

**The limit that mattered:** a `.comment` stamp is the date of the compiler
package, not of the build. It gives a lower bound only — `reconf` was built on
or after 2000-01-16 — and that bound falls *before* the CD's own 23–25 Feb file
dates, so on its own it was equally consistent with a tool made during the
run-up to the master. Points 1 and 2 carried the conclusion; point 4 only failed
to contradict it.

The archive settles it at 2000-09-21, which is consistent with all four and
sharper than any of them. Note what the compiler stamp did *not* say: eight
months elapsed between the newest possible compiler package and the actual
build, so on a stamp alone the honest answer was always going to be a range.

## What it writes

`main` is a shell: install `SIGINT`/`SIGTERM` handlers, print `Theocracy setup
for Linux version`, build `HomeDir + "/mvos.cfg"`, call `CreateConfig`, then
print `Good luck!` / `To reconfigure please run reconf!` or `Error: Setup
aborted.`. Everything is in `CreateConfig` (`0x0804af10`), one ~17 KB function.

Two globals, both built in `__static_initialization_and_destruction_0`:

- `DEFAULT_CONFIG_NAME` = `/usr/games/theocracy_base/mvos.cfg`
- `HomeDir` = `getenv("HOME")` + `/.theocracy`

so the file it edits is `~/.theocracy/mvos.cfg` — which matches the shipped
`theocracy` launcher exactly (it does `cd ~/.theocracy` and `cp -u` the
installed `mvos.cfg` there).

The defaults it builds in memory first, and the only keys it manages:

| Section | Key | Default | Prompt |
|---|---|---|---|
| `[vmachine]` | `soundcard` | `/dev/dsp` | `Where is your sound card ?` |
| `[vmachine]` | `cdrom_device` | `/dev/cdrom` | `Where is your CD device?` |
| `[vmachine]` | `fullscreen` | `false` | `Do you want to use the game in fullscreen mode? (yes/no)` |
| `[vmachine]` | `cdrom_mountpoint` | `/mnt/cdrom` | detected; see below |
| `[game]` | `language` | `english` | `(E)nglish, (M)agyar, (S)panish, (F)rench, (G)erman or (I)talian ?` |

The two section names are pushed as literals at `0x0804af3f` (`vmachine`) and
`0x0804b455` (`game`) — read off the instruction stream, because Ghidra drops
the string arguments to the inlined `cString` constructors throughout this
function and the decompile shows bare `gcc2_compiled_(&local_20)` calls with no
visible operand.

Flow:

1. `LoadConfig(~/.theocracy/mvos.cfg)`. On failure, warn and retry
   `DEFAULT_CONFIG_NAME`. If **both** fail:
   `Error: %s not found, probably the Theocracy has not installed.` and abort —
   the in-memory defaults are *not* used to write a file from nothing. They only
   fill in keys missing from a config that did load.
2. Ask for the sound card and the CD device.
3. Work out the mount point (below). If the detected value differs from the one
   in the file, print both and ask
   `Do you want to continue to use your setting ? (yes/no)`; on `no`, ask
   `Where is the mount point of your CD device?`.
4. Language. If the value in the file is not one of the six,
   `Warning: Unknown language in configfile: '%s'!` and the confirm loop runs.
5. Fullscreen, written back as the literal strings `true` / `false`.
6. Print `\nSettings are:` and one `*> key: 'value'.` line per entry, then
   `Do you want to save it? (yes/no)`.
7. Write **the whole config**, not just the five keys: it walks the `cConfig`
   list and emits `[%s]\n` for a section node and `%s=%s\n` for an entry. On
   `fopen` failure, `Error: Cannot create file %s.\n` to stderr and abort.

Prompt conventions, from `GetAnswer` (`0x080498d0`): the current value is shown
in brackets (`%s [%s] `), input is one `fgets` of up to 512 bytes, and **an
empty line keeps the default**. Every yes/no question is a loop — the typed
answer is upper-cased and matched against `YES` / `NO`, and anything else asks
again — while the *default* offered in the brackets is the lower-case `yes` or
`no`. Config values are lower-cased before being tested (that is how an existing
`fullscreen=true` picks the right default). `GetSelection` (`0x08049980`) drives
the one-letter language choice against the string `e/m/s/f/g/i`.

The config file is opened `w+`, so a save truncates and rewrites in place.

## Mount-point detection

`FindMountPoint` (`0x08049dd4`) is the only part of the tool doing real work,
and it is the code behind a ritual that
[original-os-setup.md](original-os-setup.md) documents from the player's side.
Given the CD device node, it:

1. Searches `/etc/fstab` for the device, and takes field 2 as the mount point.
2. Failing that, searches `/etc/mtab` the same way.
3. Failing that, `readlink`s the device and retries both files against the link
   target — because `/dev/cdrom` is conventionally a symlink to the real node
   (`/dev/hdc` and friends). It loops, following a chain, and `chdir`s to the
   link's own directory so a *relative* link resolves, saving and restoring the
   original cwd with `getcwd`/`chdir` around the whole thing.
4. On total failure, `Calculation of the mount point failed!` and it returns the
   fallback it was handed.

`SearchInSysTab` (`0x08049bc0`) is the parser both steps share: `fopen(…,
"rt")`, 512-byte lines into a static buffer, truncate at the newline, skip
leading spaces and tabs, ignore `#` comment lines, split the first
whitespace-delimited field and compare it to the target, and on a match return
the second field.

## What this says about our `mvos.cfg`

`data/game/mvos.cfg` is the one file under `data/game/` that is ours — 11 lines,
hand-authored, tracked deliberately. Set against what `reconf` writes:

```
              ours (data/game/mvos.cfg)        reconf
[vmachine]    device=xf86                      —
              fullscreen=0                     fullscreen=false
              fillobjmem=0                     —
              cdrom_mountpoint=/mnt/cdrom      cdrom_mountpoint=<detected>
              —                                soundcard=/dev/dsp
              —                                cdrom_device=/dev/cdrom
[sound]       card=dummy                       —
[network]     enable=1                         —
[game]        —                                language=english
```

**Checked against `libmvos.so` the same day, and our file was wrong.** Five of
its seven lines are read by nothing at all:

| our line | read by | verdict |
|---|---|---|
| `[vmachine] device=xf86` | nobody | the engine's key is **`video`** |
| `[vmachine] fullscreen=0` | nobody | the string `fullscreen` is **absent from both binaries** |
| `[vmachine] fillobjmem=0` | `0xa5210` | real, but only a leading `'n'` disables the fill — `0` leaves it **on** |
| `[vmachine] cdrom_mountpoint=/mnt/cdrom` | `VM_GetCDRomName` | real, and equal to the built-in default |
| `[sound] card=dummy` | nobody | the engine's key is **`[vmachine] soundcard`** |
| `[network] enable=1` | nobody | the string `network` is **absent from both binaries** |
| *(no `cdrom_device`)* | `GetCDRomDeviceName` | absent → default `/dev/cdrom` |

So the config we shipped was **functionally empty** — every live setting either
missing or already at its default — which is precisely why nothing ever
misbehaved. The full engine vocabulary is five keys and is tabulated in
[application-bootstrap.md](../subsystems/application-bootstrap.md), whose own
"config vocabulary" line was wrong in the same two places and is now corrected.

The fix did not come from `reconf` but from what it pointed at: **`inst.linux`
writes `mvos.cfg` with `printf` formats that spell the whole schema out** —
`soundcard=%s`, `cdrom_device=%s`, `cdrom_mountpoint=%s`, the literal
`fullscreen=false`, `[game]`, `language=%s`, under `[vmachine]`. That is the
authentic installed file, and `data/game/mvos.cfg` is now exactly it. Every
value equals the default the engine would have picked anyway, so the change is
behaviour-neutral by construction — and **confirmed by play on 2026-08-15**:
boot, the `xf86` default video path, English menu text, sound and a CD music
track. That run was worth insisting on for a reason the analysis could not
cover. The *values* were verified by decompiling each reader, but the file's
**shape** was not: the new file drops the blank lines between sections, and
libmvos' own parser (`IdentifyFileSystemMvosCfg`, `0xa4640`) has never been
read. Reasoning had established what the keys resolve to, not that the parser
accepts the layout they arrive in.

Two things worth keeping straight:

- **`fullscreen` is inert in the original game**, not merely under this port.
  The installer writes it, `reconf` toggles it, and no code in either shipped
  binary ever looks it up. The port's `THEOC_FULLSCREEN` is not a replacement
  for a working key; there was never a working key.
- **`[game] language` is read by the game, not the engine** — `theocracy.real`
  holds `game`, `language`, `english`, `data/locale/` and `.sdb` in one string
  block, i.e. it composes `data/locale/<language>.sdb`. `libmvos.so` contains
  none of those.

The reading was done with `libmvos.so` open in Ghidra; the `theocracy.real`
statements above are from exhaustive byte searches of the file on disk, since
the MCP shows one program at a time and that one was not loaded. No
`theocracy.real` addresses are quoted here for that reason.

## What it does *not* do

It is a configuration editor and nothing more — no patching, no file
installation, no data migration. That is exhaustive rather than impressionistic:
the binary has **34 dynamic imports and zero `int 0x80` in `.text`**, so those
imports are the complete OS boundary. Absent from them: `unlink`/`remove`/
`rename` (it cannot delete or move anything), `mkdir`/`chmod`/`chown`,
`system`/`exec*`/`fork`/`popen` (it cannot launch anything, including the
installer), `open`/`read`/`write`/`ioctl`, `stat`/`access`, sockets, `dlopen`.
The `pthread_*` entries are libgcc's exception machinery detecting a threaded
libc — `pthread_create` never even reaches the PLT.

There are exactly **three `fopen` sites** in the whole binary:

| Site | Mode | Target |
|---|---|---|
| `SearchInSysTab` | `rt` | `/etc/fstab`, `/etc/mtab` |
| `LoadConfig` | `r` | `mvos.cfg` |
| `CreateConfig` | `w+` | **the only write** |

and the write takes `main`'s `HomeDir + "/mvos.cfg"`, so it only ever writes
`~/.theocracy/mvos.cfg`. The system-wide copy is read as a fallback template and
never written — unlike `inst.linux`, `reconf` needs no root and cannot damage
the installed config.

The signal handlers are nothing: `SIGINT`/`SIGTERM` prints `Setup interrupted.`
and `exit(0)`, and `signal_handler_null` is a literal no-op used to ignore
`SIGINT` while the banner prints.

So whatever Philos shipped this to fix in September 2000 was fixable by editing
five config values — most plausibly the CD mount-point detection, which is the
only part of the tool doing real work.

## A free cross-check on the container classes

`reconf` statically links `cString`, `cConfig`, `cConfigEntry`, `cList` and
`cNode` **with a `.symtab`**, which no other binary in this project gives us —
`theocracy.real` is `.symtab`-stripped and libmvos' copies are read through
mangled dynamic exports. The named functions here
(`GetEntry__C7cConfigRC7cString`, `GetSection__C7cConfigRC7cString`,
`GetSectionLast__C7cConfigP12cConfigEntry`, `GetFirst__C7cConfig`,
`UnLink__5cNode`, `UnLinkList__5cList`, `GetMiddle__C7cStringRCUlT1`,
`__vc__C7cStringRCUl`) are a confirmation surface for
[memory-and-containers.md](../subsystems/memory-and-containers.md).

Two layouts fall straight out of the code above:

- **`cString` is 12 bytes**: `char *ptr; unsigned len; char valid;` — every
  access in this binary tests the `valid` byte at `+0x08` and calls
  `Fatal(...)` with a `strings.hpp` line number if it is clear.
- **`cConfigEntry` is `0x24` bytes**: `cNode` links at `+0x00`/`+0x04`, the key
  `cString` at `+0x08`, the value `cString` at `+0x14`, and an **is-section
  flag at `+0x20`**. Every entry walk in `CreateConfig` skips nodes whose
  `+0x20` is non-zero, and the writer branches on exactly that byte to choose
  between `[%s]\n` and `%s=%s\n`.

These are read off `reconf`, so they are evidence about *its* statically linked
copy. They should agree with libmvos' — but that is a prediction until someone
checks it, not a transfer.

## Open threads

- `LoadConfig` (`0x0804a708`) was not read line by line, though it is now
  bounded: one `fopen(path, "r")` and no other file access. Its behaviour is
  inferred from its two callers and its two error strings
  (`Invalid section in line %lu`, `Entry without section in line %lu`). It
  parses the same INI shape the writer emits.
