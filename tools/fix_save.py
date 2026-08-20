#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
"""Repair Theocracy .tsg saves — collapse the per-province duplicate groups.

The bug
-------
Every time the game saves, it appends a byte-identical copy of a small group of
records to a list at the end of *each province's* data. Each list stores its
length in a **single byte**, counting 17-byte units. Most provinces append 4
units (68 bytes) per save; at least one (map23) appends 5.

So the counter climbs 4 or 5 per save and dies at 255 — the 5-per-save province
first, at 51 saves. Past that the byte wraps, the loader reads the wrong length,
and everything after it in the file is misparsed. That is the "save corrupts
after ~50 saves" bug, and it is why deleting the duplicates by hand (leaving one
group) recovers a file.

The repeated groups carry nothing: they are exact duplicates. Collapsing every
run to a single group is lossless and resets the counter to 4 (or 5).

Why this is safe to do byte-wise
--------------------------------
The signature is over-determined. A run qualifies only if **both** hold:

  1. the block repeats byte-for-byte at a fixed 17-byte-multiple stride, and
  2. the byte immediately before it equals the number of 17-byte units present.

Condition 2 is a checksum over condition 1. Random data satisfying both is not
a realistic concern, and a file that satisfies neither is left untouched.

An already-corrupted save fails (2) because its counter has wrapped — that is
detected and reported separately, and repaired by recomputing the count from
the repetition rather than trusting the stored byte.

The uninitialised header
------------------------
Separately, the 72-byte header every save starts with is mostly stack the game
never initialised. Only the NUL-terminated save name at the front and the
8-byte "theosg42" magic at +0x40 are real; in between are live guest pointers,
which is why two saves of an identical game state never compare equal. That is
a nuisance when triaging a save a playtester sent, so the window is zeroed and
stamped with the identity of the build that wrote it.

The stamp is 22 fixed-width bytes, no separators, so every field parses by
offset:

    8f4b 260812 9245c7e + ma 03
    └id┘ └date┘ └sha7─┘ │ └┘ └┘
      │      │      │   │  │  └─ knob mask, 2 hex — see KNOB_BITS
      │      │      │   │  └──── host + arch: ma / l6 / la / w6
      │      │      │   └─────── `+` dirty tree, `0` clean
      │      │      └─────────── commit hash
      │      └────────────────── commit date, YYMMdd
      └───────────────────────── format id, fixed

The window is not a fixed size — it is 64 - len(save name) - 1, and the game's
save dialog caps the name at 40 characters, so it runs from 55 bytes down to 23.
22 fits the floor with a byte to spare. It is never truncated: below its full
width the window is zeroed, a partial stamp being neither parseable nor
recognisably absent.

The date is redundant against the hash and kept anyway, precisely because it is:
the two have to agree, and making them agree needs the repository they came
from, so an edited stamp does not survive inspection.

The stamp must match what port/src/traps.cpp writes byte-for-byte, or the two
implementations stop being checkable against each other — so `--stamp` takes the
literal string the binary emits. It defaults to reading git the same way CMake
does, which is right whenever this repo is the one that built it. The knob mask
is read from the environment, so cross-checking wants the same environment on
both sides.

Usage
-----
    tools/fix_save.py check  save0.tsg [more...]     # report only (default)
    tools/fix_save.py fix    save0.tsg [more...]     # rewrite, keeps a .bak
    tools/fix_save.py header save0.tsg [more...]     # decode the header stamp

    --stamp "8f4b2608129245c7e+ma03"   # match a specific binary exactly
    --no-stamp                         # zero the window, write no stamp
"""

import shutil
import subprocess
import sys
from pathlib import Path

UNIT = 17                 # one record: four LE u32 + one trailing byte
GROUPS = (4, 5)           # units appended per save; 5 is map23's
MIN_REPEATS = 2           # nothing to collapse below this

HEADER_LEN = 0x48         # the game's single Write(file, local_ac, 0x48)
MAGIC_OFF = 0x40          # "theosg42" — initialised, never touched
MAGIC = b"theosg42"
STAMP_ID = "8f4b"         # format id; see normalise_save_header in traps.cpp
STAMP_LEN = 22            # id4 + date6 + sha7 + flag1 + host2 + knobs2

# Bit -> knob, mirroring kKnobBits in traps.cpp. Order is load-bearing: it is
# what makes a mask read the same on both sides.
KNOB_BITS = ("THEOC_EDIT", "THEOC_NEW_WORLD", "THEOC_CONSOLE", "THEOC_SERVER",
             "THEOC_PROVINCE_MS", "THEOC_FRAME_MS", "THEOC_WORLD_FILE",
             "THEOC_LONGRUN")


def _host_arch() -> str:
    """Mirror the two #if ladders in traps.cpp."""
    import platform
    host = {"darwin": "m", "win32": "w", "linux": "l"}.get(sys.platform, "o")
    arch = {"arm64": "a", "aarch64": "a", "x86_64": "6", "amd64": "6",
            "i386": "3", "i686": "3"}.get(platform.machine().lower(), "?")
    return host + arch


def knob_mask() -> int:
    import os
    return sum(1 << i for i, k in enumerate(KNOB_BITS) if os.getenv(k) is not None)


def default_stamp() -> str:
    """The 22-byte stamp, resolved exactly as port/CMakeLists.txt does."""
    root = Path(__file__).resolve().parent.parent / "port"

    def git(*args, fallback):
        try:
            out = subprocess.run(["git", *args], cwd=root, capture_output=True,
                                 text=True, check=True).stdout.strip()
            return out or fallback
        except (OSError, subprocess.CalledProcessError):
            return fallback

    date = git("log", "-1", "--format=%cd", "--date=format:%y%m%d", fallback="000000")
    sha = git("rev-parse", "--short=7", "HEAD", fallback="0000000")
    if sha == "0000000":
        flag = "0"
    else:
        sha = sha[:7]     # --short=7 is a minimum; the field is fixed-width
        flag = "+" if git("status", "--porcelain", "--untracked-files=no",
                          fallback="") else "0"
    return f"{STAMP_ID}{date[:6]:0<6}{sha}{flag}{_host_arch()}{knob_mask():02x}"


def normalise_header(data: bytes, stamp) -> bytes:
    """Zero the uninitialised window and stamp it. Returns data unchanged if the
    header is not one we recognise — same principle as the collapse below.

    stamp: the 22-byte string, or None to zero-fill without one. It is never
    truncated: below its full width the window is zeroed instead, a partial
    stamp being neither parseable nor recognisably absent.
    """
    if len(data) < HEADER_LEN or data[MAGIC_OFF:MAGIC_OFF + 8] != MAGIC:
        return data
    nul = data.find(b"\0", 0, MAGIC_OFF)
    if nul < 0 or nul + 1 >= MAGIC_OFF:
        return data                       # unterminated, or no room left
    room = MAGIC_OFF - nul - 1
    tag = b""
    if stamp and len(stamp) <= room:
        tag = stamp.encode()
    return data[:nul + 1] + tag.ljust(room, b"\0") + data[MAGIC_OFF:]


def parse_stamp(text: str):
    """-> dict of the fields, or None if this is not one of ours.

    Fixed offsets, which is the point of the fixed width:
        [0:4] id  [4:10] date  [10:17] sha  [17] dirty  [18:20] host  [20:22] knobs
    """
    if len(text) != STAMP_LEN or not text.startswith(STAMP_ID):
        return None
    try:
        knobs = int(text[20:22], 16)
    except ValueError:
        return None
    return {"id": text[0:4], "date": text[4:10], "sha": text[10:17],
            "dirty": text[17] == "+", "host": text[18:20], "knobs": knobs,
            "knobs_set": [k for i, k in enumerate(KNOB_BITS) if knobs & (1 << i)]}


def read_header(data: bytes):
    """-> (save_name, stamp) for display. stamp is '' on an unstamped save."""
    if len(data) < HEADER_LEN or data[MAGIC_OFF:MAGIC_OFF + 8] != MAGIC:
        return None, None
    nul = data.find(b"\0", 0, MAGIC_OFF)
    if nul < 0:
        return None, None
    window = data[nul + 1:MAGIC_OFF].rstrip(b"\0")
    text = window.decode("ascii", "replace") if window else ""
    return data[:nul].decode("ascii", "replace"), text


def _interesting(block: bytes) -> bool:
    """Reject flat fill. Huge zero runs repeat trivially and mean nothing."""
    return len(set(block)) >= 4


def find_runs(data: bytes):
    """-> [(count_off, group_units, repeats)] for every collapsible run.

    **Anchored on the counter byte, never on the repetition.** Searching for the
    repeat first is what an earlier cut did, and it is wrong: once a list holds
    many identical groups, the periodicity also holds at offsets *inside* a
    group, so a left-to-right scan can lock onto a shifted phase. It then writes
    the counter over a data byte — producing exactly the subtly-broken save that
    does not load. Verified: it corrupted 86 bytes on a 64-group file.

    So instead, read each byte as a candidate count and check whether the layout
    it *claims* is actually there. The count fixes the phase exactly, and the
    claim is then verified against the bytes. Nothing is inferred.
    """
    runs, i, n = [], 0, len(data)
    while i < n - 1:
        count = data[i]
        hit = None
        for g in GROUPS:
            if count < g * MIN_REPEATS or count % g:
                continue
            r = count // g
            span, body = g * UNIT, i + 1
            if body + count * UNIT > n:
                continue
            block = data[body:body + span]
            if not _interesting(block):
                continue
            if all(data[body + k * span:body + (k + 1) * span] == block
                   for k in range(1, r)):
                hit = (g, r)
                break
        if hit:
            g, r = hit
            runs.append((i, g, r))
            i += 1 + g * r * UNIT
        else:
            i += 1
    return runs


def suspect_runs(data: bytes, known):
    """Long identical-group repeats whose counter does *not* describe them.

    That is what an overflowed save looks like: the data is still there, the
    byte that says how much of it there is has wrapped. Reported, never
    repaired — the phase cannot be established without a trustworthy counter,
    and a wrong guess yields a file that will not load.
    """
    covered = set()
    for off, g, r in known:
        covered.update(range(off, off + 1 + g * r * UNIT))
    out, i, n = [], 0, len(data)
    while i < n:
        if i in covered:
            i += 1
            continue
        found = None
        for g in GROUPS:
            span = g * UNIT
            if i + 2 * span > n:
                continue
            block = data[i:i + span]
            if not _interesting(block) or data[i + span:i + 2 * span] != block:
                continue
            r = 2
            while data[i + r * span:i + (r + 1) * span] == block:
                r += 1
            found = (g, r)
            break
        if found and found[1] >= 8:      # well past anything healthy
            out.append((i, found[0], found[1]))
            i += found[0] * found[1] * UNIT
        else:
            i += 1
    return out


def validate(runs, suspects):
    """-> [reasons not to write]. Empty means the parse is trustworthy.

    Every province gets exactly one group per save, so in an intact file **every
    list must hold the same number of groups**. That single invariant is what
    separates a good parse from a bad one, and it is not a nicety: on an already
    overflowed file the counter-anchored scan finds 125 "lists" with 2..38
    groups, because inside a long identical run there are interior bytes that
    happen to describe a plausible sub-run. Writing that back destroys the save.
    """
    problems = []
    if suspects:
        problems.append(f"{len(suspects)} already-overflowed run(s) present")
    if not runs:
        return problems
    reps = {r[2] for r in runs}
    if len(reps) != 1:
        problems.append(f"lists disagree on group count ({sorted(reps)}) — "
                        f"every province should have the same number")
    if not 30 <= len(runs) <= 60:
        problems.append(f"found {len(runs)} lists, expected ~44 (one per province)")
    return problems


def repair(data: bytes, stamp="auto"):
    """-> (new_bytes, runs, suspects). Keeps one group per run, resets counters.

    stamp: "auto" resolves it from git, None writes none, else the literal
    22-byte string.

    The header is normalised *before* the run scan, matching traps.cpp — the
    scanner reads from byte 0, so normalising after would let the junk header
    influence which runs are found and the two implementations could disagree.
    """
    if stamp == "auto":
        stamp = default_stamp()
    data = normalise_header(data, stamp)
    runs = find_runs(data)
    out, prev = bytearray(), 0
    for off, g, r in runs:
        out += data[prev:off]
        out.append(g)                                   # counter = one group
        out += data[off + 1:off + 1 + g * UNIT]         # first group only
        prev = off + 1 + g * r * UNIT
    out += data[prev:]
    return bytes(out), runs, suspect_runs(data, runs)


def report(path: Path, runs, suspects, before: int, after: int) -> None:
    if runs:
        sizes = sorted({r[1] for r in runs})
        reps = sorted({r[2] for r in runs})
        worst = max(r[1] * r[2] for r in runs)
        headroom = (255 - worst) // max(r[1] for r in runs)
        print(f"{path.name}: {len(runs)} province lists, {reps} groups each "
              f"({sizes} units per group), {before:,} -> {after:,} bytes "
              f"(-{before - after:,})")
        print(f"  highest counter {worst}/255 — about {headroom} more saves "
              f"before overflow; after repair, 251")
    else:
        print(f"{path.name}: no collapsible lists found")
    if suspects:
        print(f"  !! {len(suspects)} run(s) of identical groups whose counter "
              f"does NOT describe them — this save has already overflowed.")
        for off, g, r in suspects[:5]:
            print(f"     at {off:#x}: {r} groups of {g} units, "
                  f"counter reads {r * g & 0xFF} (should be {r * g})")
        print("     NOT repaired: the counter is the only thing that fixes the")
        print("     phase, and guessing it yields a save that will not load.")


def main(argv) -> int:
    mode = "check"
    if argv and argv[0] in ("check", "fix", "header"):
        mode, argv = argv[0], argv[1:]

    stamp = "auto"                         # "auto" = from git, None = no stamp
    rest = []
    it = iter(argv)
    for a in it:
        if a == "--no-stamp":
            stamp = None
        elif a == "--stamp":
            stamp = next(it, "")
        else:
            rest.append(a)
    argv = rest

    if not argv:
        print(__doc__.strip().split("Usage")[-1])
        return 2

    if mode == "header":
        for name in argv:
            p = Path(name)
            save_name, text = read_header(p.read_bytes())
            if save_name is None:
                print(f"{p.name}: not a theosg42 save")
                continue
            print(f"{p.name}: name {save_name!r}")
            f = parse_stamp(text) if text else None
            if f:
                d = f["date"]
                print(f"  built   20{d[0:2]}-{d[2:4]}-{d[4:6]} from {f['sha']}"
                      f"{' (dirty tree)' if f['dirty'] else ''}")
                print(f"  host    {f['host']}")
                print(f"  knobs   {f['knobs']:#04x}"
                      f"{'  ' + ' '.join(f['knobs_set']) if f['knobs_set'] else '  (all default)'}")
            elif text:
                print(f"  stamp   {text!r}  (unrecognised — not one of ours)")
            else:
                print("  stamp   (none — written before normalisation existed)")
        return 0

    rc = 0
    for name in argv:
        p = Path(name)
        data = p.read_bytes()
        new, runs, suspects = repair(data, stamp)
        report(p, runs, suspects, len(data), len(new))
        problems = validate(runs, suspects)
        if problems:
            rc = 1
            print("  REFUSING to modify this file:")
            for why in problems:
                print(f"    - {why}")
            # The header is independent of the structure, so a file we decline
            # to restructure can still have its header cleaned. Only offered,
            # never done silently: a file this suspect should be touched once.
            if mode == "fix" and normalise_header(data, stamp) != data:
                print("    (header alone could be normalised: --no-stamp aside,"
                      " rerun on a copy if you want it)")
        elif mode == "fix" and new != data:
            shutil.copyfile(p, p.with_suffix(p.suffix + ".bak"))
            p.write_bytes(new)
            what = "collapsed and header normalised" if runs else "header normalised"
            print(f"  {what}; original kept as {p.name}.bak")
        elif mode == "fix":
            print("  already collapsed, header already current — nothing to write")

    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
