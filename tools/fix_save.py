#!/usr/bin/env python3
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

Usage
-----
    tools/fix_save.py check  save0.tsg [more...]     # report only (default)
    tools/fix_save.py fix    save0.tsg [more...]     # rewrite, keeps a .bak
"""

import shutil
import sys
from pathlib import Path

UNIT = 17                 # one record: four LE u32 + one trailing byte
GROUPS = (4, 5)           # units appended per save; 5 is map23's
MIN_REPEATS = 2           # nothing to collapse below this


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


def repair(data: bytes):
    """-> (new_bytes, runs, suspects). Keeps one group per run, resets counters."""
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
    if argv and argv[0] in ("check", "fix"):
        mode, argv = argv[0], argv[1:]
    if not argv:
        print(__doc__.strip().split("Usage")[-1])
        return 2
    rc = 0
    for name in argv:
        p = Path(name)
        data = p.read_bytes()
        new, runs, suspects = repair(data)
        report(p, runs, suspects, len(data), len(new))
        problems = validate(runs, suspects)
        if problems:
            rc = 1
            print("  REFUSING to modify this file:")
            for why in problems:
                print(f"    - {why}")
        elif mode == "fix" and runs and new != data:
            shutil.copyfile(p, p.with_suffix(p.suffix + ".bak"))
            p.write_bytes(new)
            print(f"  written; original kept as {p.name}.bak")
        elif mode == "fix":
            print("  already collapsed — nothing to write")

    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
