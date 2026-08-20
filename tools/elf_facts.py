#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
"""Throwaway M1 fact-check: confirm the docs' ELF claims for theocracy.real
before writing the C++ loader. Prints the ground truth the loader needs."""
import sys
from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection
from elftools.elf.relocation import RelocationSection
from elftools.elf.dynamic import DynamicSection

PATH = sys.argv[1] if len(sys.argv) > 1 else "linux/theocracy.real"
DOC_ADDRS = {
    "Init__12cApplication": 0x8144600,
    "Start__12cApplicationiPPc": 0x8144650,
    "RollingDemoFrame__Fv": 0x8063540,
}
CTORS_DOC = (0x8597400, 0x8597763)

with open(PATH, "rb") as fh:
    elf = ELFFile(fh)
    hdr = elf.header
    print("== ELF header ==")
    print(f"  type      {hdr['e_type']}")
    print(f"  machine   {hdr['e_machine']}")
    print(f"  entry     {hdr['e_entry']:#x}")
    print(f"  phnum     {hdr['e_phnum']}   shnum {hdr['e_shnum']}")

    print("\n== PT_LOAD segments ==")
    min_va = None
    for seg in elf.iter_segments():
        if seg['p_type'] != 'PT_LOAD':
            continue
        va, msz, fsz = seg['p_vaddr'], seg['p_memsz'], seg['p_filesz']
        off, flags = seg['p_offset'], seg['p_flags']
        fl = "".join(c if flags & b else '-' for c, b in [('R',4),('W',2),('X',1)])
        min_va = va if min_va is None else min(min_va, va)
        print(f"  va {va:#010x} memsz {msz:#08x} filesz {fsz:#08x} off {off:#08x} {fl}"
              f"  bss={msz-fsz:#x}")
    print(f"  -> lowest PT_LOAD vaddr = {min_va:#x} (doc says base 0x08048000)")

    print("\n== sections of interest ==")
    for name in ('.ctors', '.dtors', '.init', '.plt', '.got', '.got.plt',
                 '.bss', '.dynsym', '.dynstr', '.rel.plt', '.rel.dyn',
                 '.rel.bss', '.data', '.text'):
        s = elf.get_section_by_name(name)
        if s:
            print(f"  {name:10} addr {s['sh_addr']:#010x} size {s['sh_size']:#08x} "
                  f"off {s['sh_offset']:#08x} entsz {s['sh_entsize']:#x}")

    ct = elf.get_section_by_name('.ctors')
    if ct:
        import struct
        fh.seek(ct['sh_offset'])
        raw = fh.read(ct['sh_size'])
        words = struct.unpack("<%dI" % (len(raw)//4), raw)
        print(f"\n== .ctors ({len(words)} words) doc range {CTORS_DOC[0]:#x}-{CTORS_DOC[1]:#x} ==")
        print(f"  addr {ct['sh_addr']:#x}..{ct['sh_addr']+ct['sh_size']:#x}")
        print(f"  first={words[0]:#x} last={words[-1]:#x}  "
              f"fn ptrs (excl 0/-1): {sum(1 for w in words if w not in (0,0xffffffff))}")

    print("\n== relocations ==")
    for sec in elf.iter_sections():
        if not isinstance(sec, RelocationSection):
            continue
        from collections import Counter
        types = Counter(r['r_info_type'] for r in sec.iter_relocations())
        print(f"  {sec.name:10} n={sec.num_relocations():5}  types={dict(types)}")

    # dynamic symbols: exports vs UND imports
    dsym = elf.get_section_by_name('.dynsym')
    exports, imports = [], []
    for sym in dsym.iter_symbols():
        if sym.name == '':
            continue
        if sym['st_shndx'] == 'SHN_UNDEF':
            imports.append(sym.name)
        else:
            exports.append((sym.name, sym['st_value']))
    print(f"\n== dynsym: {len(exports)} defined exports, {len(imports)} UND imports ==")

    print("\n== doc callback addresses ==")
    exp_by_name = dict(exports)
    for nm, doc in DOC_ADDRS.items():
        got = exp_by_name.get(nm)
        tag = "MATCH" if got == doc else f"!! got {got:#x}" if got else "MISSING"
        print(f"  {nm:28} doc {doc:#x}  {tag}")

    # R_386_COPY (type 5) targets = copy-reloc'd singletons/vtables
    relbss = None
    for sec in elf.iter_sections():
        if isinstance(sec, RelocationSection):
            for r in sec.iter_relocations():
                if r['r_info_type'] == 5:  # R_386_COPY
                    relbss = relbss or []
                    sym = dsym.get_symbol(r['r_info_sym'])
                    relbss.append((r['r_offset'], sym.name, sym['st_size']))
    if relbss:
        print(f"\n== R_386_COPY relocs ({len(relbss)}) ==")
        for off, nm, sz in relbss[:40]:
            print(f"  {off:#010x} size {sz:#06x}  {nm}")
        if len(relbss) > 40:
            print(f"  ... +{len(relbss)-40} more")
