#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
"""Enumerate every indirect call and jump site in both binaries.

Why this exists
---------------
`subtree.py` stops at `call *%eax` and says so: each one is a point where the
call closure cannot be proven, and 356 of them sit under `SimulationStep`
alone.  Static analysis cannot cross them -- the target is a register.  The
emulator can: it *executes* them, and THEOC_ICALL records where each one went
(port/src/machine.cpp, "indirect-call edge log").

This is the static half.  It emits the site table the host loads, keyed by the
**return address** (site + instruction length) rather than the site, because
that is what the host can match cheaply: Unicorn ends a basic block at a call
or jump, so a block whose end address equals a site's return address is that
site having just executed, with no guest memory read and no disassembler in the
host.

Output (TSV, `data/indirect_sites.tsv` by default):

    image  site  retaddr  kind  func  text

`site` and `retaddr` are **file** addresses; the host adds the load bias for
mvos and nothing for the game, exactly as tools/ghidra_funclist.py describes.
`func` is the containing function from the ELF symbol table, for reading the
file by eye; the host ignores it and writes addresses only.

`--resolve <icall.tsv>` goes the other way: it annotates a run's output with
those names, so the host never has to carry a symbol table for a diagnostic.
Symbols come from objdump's own labels rather than a Ghidra export -- one less
thing to keep in sync, and Ghidra's function boundaries are not authoritative
anyway (docs/reference/re-methodology.md).
"""
import argparse, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGES = {
    'mvos': 'data/cd/linux/libmvos.so.0.9',
    'game': 'data/cd/linux/theocracy.real',
}

# Same shape as subtree.py's CALL, plus `jmp`: an indirect *jump* is a tail call
# (the PLT thunks into libmvos are all `jmpl *0x...`) or a switch table, and
# both are closure-breaking in exactly the same way a `call *` is.
INSN = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(call|jmp)l?\s+(\*\S+)')


LABEL = re.compile(r'^([0-9a-f]+) <([^>]+)>:')


def disasm(image):
    path = os.path.join(ROOT, IMAGES[image])
    if not os.path.exists(path):
        sys.exit(f'{IMAGES[image]} not present -- the binaries are not in git')
    return subprocess.run(['objdump', '-d', path],
                          capture_output=True, text=True).stdout


def load_funcs(text):
    """Entry point -> name, from objdump's own labels."""
    rows = [(int(m.group(1), 16), m.group(2))
            for m in (LABEL.match(l) for l in text.splitlines()) if m]
    rows.sort()
    return rows


def containing(rows, addr):
    lo, hi = 0, len(rows)
    while lo < hi:
        mid = (lo + hi) // 2
        if rows[mid][0] <= addr: lo = mid + 1
        else: hi = mid
    return rows[lo - 1][1] if lo else None


def scan(image):
    out = disasm(image)
    rows = load_funcs(out)
    sites = []
    for line in out.splitlines():
        m = INSN.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        nbytes = len(m.group(2).split())
        sites.append((image, addr, addr + nbytes, m.group(3),
                      containing(rows, addr), line.strip().split('\t')[-1]))
    return sites



# Three shapes a measured edge can have, and only the first is a call. The
# other two are artefacts of *how* the edge is measured (the host comment in
# port/src/machine.cpp has the mechanism); reading them as calls would invent
# control flow that never happened.
#
#   call     the target is an exact symbol entry point -- a real dispatch
#   hostret  the target is the site's own return address, so the callee ran as
#            host code (an HLE'd import or a native override) and executed no
#            guest block; what was seen is the resumption, not the callee
#   unknown  the target is inside some function this binary does not name
#
# `unknown` is deliberately one bucket and not three. Separating a jump table
# from a tail thunk from a wrong function boundary needs a *function map*, and
# the game binary carries 444 ELF labels for what Ghidra splits into 8630
# functions -- so `containing address` here would attribute half the image to
# whichever symbol happens to precede it, and then name it. A wrong name is
# worse than no name, and it propagates (docs/reference/re-methodology.md §12).
# Anything holding a real function map can subdivide `unknown` itself.
def classify(site, ret, target, entries):
    if ret is not None and target == ret: return 'hostret'
    if target in entries: return 'call'
    return 'unknown'


def resolve_run(path, out):
    sites, names, entries = {}, {}, {}
    for im in sorted(IMAGES):
        rows = load_funcs(disasm(im))
        names[im] = dict(rows)
        entries[im] = {a for a, _ in rows}
        for _, site, ret, kind, _, _ in scan(im):
            sites[(im, site)] = ret

    full = path if os.path.isabs(path) else os.path.join(ROOT, path)
    if not os.path.exists(full):
        sys.exit(f'{path}: no such file -- run the game with THEOC_ICALL=1 first')

    counts, rows_out = {}, []
    for line in open(full).read().splitlines()[1:]:
        p = line.split('\t')
        if len(p) < 6: continue
        im, tim = p[0], p[3]
        try:
            site, target, n = int(p[1], 16), int(p[4], 16), int(p[5])
        except ValueError:
            continue
        if tim == 'host':
            cls, tfn = 'hostcall', '(host trap)'
        else:
            cls = classify(site, sites.get((im, site)), target, entries.get(tim, ()))
            tfn = names.get(tim, {}).get(target, '?')
        counts[cls] = counts.get(cls, 0) + 1
        rows_out.append((im, site, p[2], names.get(im, {}).get(site, '?'),
                         cls, tim, target, tfn, n))

    f = sys.stdout if out == '-' else open(out, 'w')
    try:
        f.write('image\tsite\tkind\tsite_sym\tclass\ttgt_image\ttarget'
                '\ttarget_sym\tcount\n')
        for im, site, kind, sfn, cls, tim, target, tfn, n in sorted(rows_out):
            f.write(f'{im}\t{site:08x}\t{kind}\t{sfn}\t{cls}\t'
                    f'{tim}\t{target:08x}\t{tfn}\t{n}\n')
    finally:
        if f is not sys.stdout: f.close()
    tot = sum(counts.values())
    print(f'{tot} edges:', file=sys.stderr)
    for k in sorted(counts):
        print(f'  {k:9s} {counts[k]:5d}', file=sys.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', '--out', default='data/indirect_sites.tsv')
    ap.add_argument('--image', choices=sorted(IMAGES), action='append',
                    help='default: both')
    ap.add_argument('--resolve', metavar='ICALL_TSV',
                    help="annotate a THEOC_ICALL run's output with function "
                         "names and edge classes; '-o -' for stdout")
    a = ap.parse_args()

    if a.resolve:
        dest = a.out
        if dest == 'data/indirect_sites.tsv': dest = '-'
        resolve_run(a.resolve, dest if dest == '-' or os.path.isabs(dest)
                    else os.path.join(ROOT, dest))
        return

    images = a.image or sorted(IMAGES)
    sites = []
    for im in images:
        s = scan(im)
        print(f'{im}: {len(s)} indirect sites '
              f'({sum(1 for x in s if x[3] == "call")} call, '
              f'{sum(1 for x in s if x[3] == "jmp")} jmp)', file=sys.stderr)
        sites += s

    dest = a.out if os.path.isabs(a.out) else os.path.join(ROOT, a.out)
    with open(dest, 'w') as f:
        f.write('image\tsite\tretaddr\tkind\tfunc\ttext\n')
        for im, site, ret, kind, fn, text in sites:
            f.write(f'{im}\t{site:08x}\t{ret:08x}\t{kind}\t{fn}\t{text}\n')
    print(f'wrote {len(sites)} sites to {a.out}', file=sys.stderr)


if __name__ == '__main__':
    main()
