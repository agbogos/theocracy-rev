#!/usr/bin/env python3
"""elfq — query the Theocracy binaries straight from the ELF file.

Why this exists
---------------
The Ghidra MCP shows **one program at a time**, so half of any cross-binary
question ("what does the game import from libmvos?", "who calls this?") arrives
when the wrong image is open. This answers those from the file instead, with no
Ghidra and no dependencies beyond the standard library.

It also exists because the ELF boilerplate underneath was hand-rolled once per
question and got it wrong at least once, in a way that produced a confidently
wrong answer: a vtable scan that only looked at `.rel.dyn`/`.rel.data` found no
relocations for libmvos (which keeps them in **`.rel.rodata`**), read the
unrelocated zeros, and concluded a virtual slot was NULL. See
docs/reference/re-methodology.md §10. `reloc_map()` here scans *every* `.rel*`
section, once, correctly.

Address spaces (docs/reference/re-methodology.md §1)
----------------------------------------------------
Everything below is in **link/file addresses** — what is actually in the ELF.
Ghidra shows libmvos at base 0x10000, so for that image `ghidra = link +
0x10000`; theocracy.real is loaded where it links (0x08048000) and the two
agree. Output marks the Ghidra column whenever it differs, so an address can be
pasted into Ghidra without re-deriving the offset by hand.

Usage
-----
    tools/elfq.py <image> <verb> [args]

<image> is a path, or one of the shorthands: game, mvos, server, vvc, keyboard,
mouse, pointer  (resolved under $THEOC_CD or data/cd/linux).

Verbs
-----
    sections                      section table (addr / file offset / size)
    sym <name>                    look a dynamic symbol up by name
    sym @<addr>                   ... or by address (nearest preceding)
    plt [filter]                  PLT stub -> imported symbol
    xref-call <addr|name>         direct CALL/JMP rel32 sites targeting it
    xref-global <addr> [-f N]     code touching a global; -f N = byte field +N
    vtable <addr> [-n N]          dump N words with relocations applied
    strrefs <lo> <hi>             string literals referenced in a code range

Every verb is read-only and side-effect free.
"""
import os
import re
import struct
import sys

SHORTHAND = {
    "game": "theocracy.real",
    "mvos": "libmvos.so.0.9",
    "server": "server",
    "vvc": "libmvos_vvc_x.so.0.9",
    "keyboard": "libmvos_keyboard_x.so.0.9",
    "mouse": "libmvos_mouse_x.so.0.9",
    "pointer": "libmvos_pointer_x.so.0.9",
}
# Ghidra project bases. libmvos is an ET_DYN linked at 0, shown at 0x10000.
GHIDRA_BASE = {"libmvos.so.0.9": 0x10000}

X86_REGS = {0: "eax", 1: "ecx", 2: "edx", 3: "ebx", 4: "esp", 5: "ebp", 6: "esi", 7: "edi"}
RELOC_TYPES = {0: "NONE", 1: "R_386_32", 2: "R_386_PC32", 5: "R_386_COPY",
               6: "R_386_GLOB_DAT", 7: "R_386_JMP_SLOT", 8: "R_386_RELATIVE"}


class Elf:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        with open(path, "rb") as fh:
            self.buf = fh.read()
        if self.buf[:4] != b"\x7fELF":
            sys.exit(f"{path}: not an ELF file")
        b = self.buf
        e_shoff, = struct.unpack_from("<I", b, 0x20)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", b, 0x2E)
        self.secs = []
        for i in range(e_shnum):
            o = e_shoff + i * e_shentsize
            (nm, typ, flags, addr, off, size,
             link, info, align, entsize) = struct.unpack_from("<10I", b, o)
            self.secs.append(dict(nm=nm, typ=typ, addr=addr, off=off,
                                  size=size, entsize=entsize))
        sh = self.secs[e_shstrndx]
        for s in self.secs:
            e = b.index(b"\0", sh["off"] + s["nm"])
            s["name"] = b[sh["off"] + s["nm"]:e].decode()
        self.by_name = {s["name"]: s for s in self.secs}
        self.ghidra_delta = GHIDRA_BASE.get(self.name, 0)
        self._relocs = None
        self._syms = None

    # -- address translation ------------------------------------------------
    def ghidra(self, va):
        return va + self.ghidra_delta

    def gtag(self, va):
        """' (ghidra 0x…)' when the two spaces differ, else ''."""
        return f" (ghidra {self.ghidra(va):#x})" if self.ghidra_delta else ""

    def off(self, va):
        """vaddr -> file offset. Goes via section headers: .data's addr and
        offset differ by a page in libmvos, which is exactly the trap that
        makes a hand-rolled `va - BASE` read the wrong bytes."""
        for s in self.secs:
            if s["name"] == ".bss" or not s["addr"] or not s["size"]:
                continue
            if s["addr"] <= va < s["addr"] + s["size"]:
                return s["off"] + (va - s["addr"])
        return None

    def word(self, va):
        o = self.off(va)
        return None if o is None else struct.unpack_from("<I", self.buf, o)[0]

    def text_range(self):
        t = self.by_name.get(".text")
        return (t["addr"], t["addr"] + t["size"]) if t else (0, 0)

    def cstr(self, va, maxlen=200):
        o = self.off(va)
        if o is None:
            return None
        e = self.buf.find(b"\0", o)
        if e < 0 or e - o > maxlen:
            return None
        raw = self.buf[o:e]
        if not raw or not all(32 <= c < 127 or c in (9, 10) for c in raw):
            return None
        return raw.decode("ascii", "replace")

    # -- symbols ------------------------------------------------------------
    def syms(self):
        if self._syms is None:
            self._syms = []
            ds, dstr = self.by_name.get(".dynsym"), self.by_name.get(".dynstr")
            if ds and dstr:
                for i in range(ds["size"] // 16):
                    o = ds["off"] + i * 16
                    nm, val, sz, info, other, shndx = struct.unpack_from("<IIIBBH", self.buf, o)
                    e = self.buf.index(b"\0", dstr["off"] + nm)
                    name = self.buf[dstr["off"] + nm:e].decode()
                    if name:
                        self._syms.append(dict(name=name, value=val, size=sz,
                                               undef=(shndx == 0), idx=i))
        return self._syms

    def sym_by_index(self, i):
        for s in self.syms():
            if s["idx"] == i:
                return s
        return None

    def sym_by_name(self, name):
        for s in self.syms():
            if s["name"] == name:
                return s
        return None

    def nearest_sym(self, va):
        best = None
        for s in self.syms():
            if s["undef"] or not s["value"] or s["value"] > va:
                continue
            if best is None or s["value"] > best["value"]:
                best = s
        return best

    # Only claim a symbol when the address is plausibly *inside* it. The game is
    # .symtab-stripped and exports 348 symbols over 2.5 MB of .text, so a plain
    # nearest-preceding lookup labels half the binary `Start__12cApplication
    # +0x158029` — a confidently wrong answer, which is worse than none.
    LABEL_MAX_DELTA = 0x1000

    def label(self, va):
        s = self.nearest_sym(va)
        if not s:
            return ""
        d = va - s["value"]
        if d == 0:
            return s["name"]
        if s["size"] and d < s["size"]:
            return f"{s['name']}+{d:#x}"
        if d <= self.LABEL_MAX_DELTA:
            return f"{s['name']}+{d:#x}?"      # '?' = beyond the declared size
        return ""

    # -- relocations --------------------------------------------------------
    def reloc_map(self):
        """r_offset -> (type, symbol-index, section). Scans EVERY .rel* section.

        libmvos keeps its vtable relocations in `.rel.rodata` and has no
        `.rel.dyn` at all, so a scan that assumes the usual names silently finds
        nothing and the caller reads unrelocated zeros as real values."""
        if self._relocs is None:
            self._relocs = {}
            for s in self.secs:
                if not s["name"].startswith(".rel") or not s["size"]:
                    continue
                for i in range(s["size"] // 8):
                    roff, info = struct.unpack_from("<II", self.buf, s["off"] + i * 8)
                    self._relocs[roff] = (info & 0xFF, info >> 8, s["name"])
        return self._relocs

    def plt_map(self):
        """PLT stub address -> imported symbol name (classic i386 layout:
        entry i of .rel.plt corresponds to .plt + 16*(i+1))."""
        out = {}
        rp, plt = self.by_name.get(".rel.plt"), self.by_name.get(".plt")
        if not rp or not plt:
            return out
        for i in range(rp["size"] // 8):
            roff, info = struct.unpack_from("<II", self.buf, rp["off"] + i * 8)
            s = self.sym_by_index(info >> 8)
            if s:
                out[plt["addr"] + 16 * (i + 1)] = s["name"]
        return out


def demangle_map():
    """name -> human signature, from the generated data/*.tsv tables."""
    out = {}
    for tsv in ("data/mvos_exports.tsv", "data/game_imports.tsv"):
        try:
            with open(tsv) as fh:
                for line in fh:
                    p = line.rstrip("\n").split("\t")
                    if len(p) >= 3 and p[1]:
                        out.setdefault(p[1], p[2])
        except OSError:
            pass
    return out


def resolve_image(spec):
    if os.path.exists(spec):
        return spec
    root = os.environ.get("THEOC_CD", "data/cd/linux")
    cand = os.path.join(root, SHORTHAND.get(spec, spec))
    if os.path.exists(cand):
        return cand
    sys.exit(f"cannot find image {spec!r} (tried {cand}); "
             f"shorthands: {', '.join(sorted(SHORTHAND))}")


def parse_addr(elf, s):
    """Accept 0x…, a decimal, or a symbol name."""
    s = s.strip()
    try:
        return int(s, 0)
    except ValueError:
        pass
    sym = elf.sym_by_name(s)
    if sym and sym["value"]:
        return sym["value"]
    sys.exit(f"cannot resolve {s!r} to an address")


# --------------------------------------------------------------------------
# verbs
# --------------------------------------------------------------------------
def v_sections(elf, args):
    print(f"{elf.path}  ({len(elf.secs)} sections)"
          + (f"   ghidra = link + {elf.ghidra_delta:#x}" if elf.ghidra_delta else ""))
    print(f"  {'name':22} {'addr':>10} {'fileoff':>9} {'size':>9}  note")
    for s in elf.secs:
        if not s["size"]:
            continue
        note = ""
        if s["addr"] and s["off"] != s["addr"]:
            note = "addr != off"
        if s["name"] == ".bss":
            note = "no file backing"
        print(f"  {s['name']:22} {s['addr']:#010x} {s['off']:#09x} {s['size']:#09x}  {note}")


def v_sym(elf, args):
    if not args:
        sys.exit("sym: need a name or @addr")
    dem = demangle_map()
    q = args[0]
    if q.startswith("@"):
        va = parse_addr(elf, q[1:])
        s = elf.nearest_sym(va)
        if not s:
            print(f"{va:#x}{elf.gtag(va)}: no preceding dynamic symbol")
            return
        d = va - s["value"]
        print(f"{va:#x}{elf.gtag(va)} = {s['name']}"
              + (f" + {d:#x}" if d else " (exact)"))
        if s["name"] in dem:
            print(f"   {dem[s['name']]}")
        return
    hits = [s for s in elf.syms() if q in s["name"]]
    if not hits:
        print(f"no dynamic symbol matching {q!r}")
        return
    for s in sorted(hits, key=lambda x: x["value"]):
        kind = "UND" if s["undef"] else f"{s['value']:#010x}"
        tag = "" if s["undef"] else elf.gtag(s["value"])
        print(f"  {kind}{tag}  {s['name']}")
        if s["name"] in dem:
            print(f"      {dem[s['name']]}")


def v_plt(elf, args):
    dem = demangle_map()
    filt = args[0] if args else None
    m = elf.plt_map()
    print(f"{len(m)} PLT stubs" + (f" matching {filt!r}" if filt else ""))
    for addr in sorted(m):
        name = m[addr]
        if filt and filt not in name:
            continue
        print(f"  {addr:#010x}{elf.gtag(addr)}  {name}")
        if name in dem:
            print(f"      {dem[name]}")


def v_xref_call(elf, args):
    if not args:
        sys.exit("xref-call: need an address or symbol")
    target = parse_addr(elf, args[0])
    lo, hi = elf.text_range()
    b, base = elf.buf, None
    t = elf.by_name[".text"]
    base = t["addr"] - t["off"]           # text is contiguous; map off<->addr
    hits = []
    for o in range(t["off"], t["off"] + t["size"] - 5):
        op = b[o]
        if op not in (0xE8, 0xE9):
            continue
        rel, = struct.unpack_from("<i", b, o + 1)
        va = o + base
        if va + 5 + rel == target:
            hits.append((va, "call" if op == 0xE8 else "jmp"))
    plt = elf.plt_map()
    what = plt.get(target) or elf.label(target) or ""
    print(f"{len(hits)} direct call/jmp sites -> {target:#x}{elf.gtag(target)}"
          + (f"  [{what}]" if what else ""))
    for va, kind in hits:
        print(f"  {va:#010x}{elf.gtag(va)}  {kind}   in {elf.label(va) or '?'}")


def v_xref_global(elf, args):
    """Find code that loads a global pointer and then touches a byte field.

    Without -f: reports every direct load of the global.
    With -f N:  also reports the byte access at +N through the loaded register,
                classified read/write, within a short instruction window.

    Deliberately conservative: it only matches an access whose base register was
    loaded from *this* global a few instructions earlier. A blind scan for
    `cmp byte [reg+N]` matches every struct in the binary that happens to use
    the same offset — that produced two false positives out of three hits when
    it was tried ad hoc, which is why it is not offered here.
    """
    if not args:
        sys.exit("xref-global: need an address or symbol")
    ptr = parse_addr(elf, args[0])
    field = None
    if "-f" in args:
        field = int(args[args.index("-f") + 1], 0)
    win = 24
    b = elf.buf
    t = elf.by_name[".text"]
    base = t["addr"] - t["off"]
    p = struct.pack("<I", ptr)

    # Byte accesses at [reg+disp], decoded properly rather than matched as a
    # fixed 2-byte prefix. The reg field of the modrm byte is an opcode
    # extension for the imm8 forms (/0, /7) and a register for the /r forms, so
    # a literal prefix like `80 40+r` silently means `add`, not `cmp`, and
    # misses every comparison in the binary.
    #   80 /7 ib  cmp byte [reg+disp], imm8     C6 /0 ib  mov byte [reg+disp], imm8
    #   F6 /0 ib  test byte [reg+disp], imm8    8A /r     mov r8, [reg+disp]
    #   88 /r     mov [reg+disp], r8            38 /r     cmp [reg+disp], r8
    #   3A /r     cmp r8, [reg+disp]            84 /r     test [reg+disp], r8
    # 80 /n ib is a whole group: only /7 (cmp) is a pure read; /0../6 are
    # read-modify-write. Missing that is how a `xor byte [flag], 1` toggle reads
    # as "never written" — which is exactly the wrong conclusion it produced
    # about the cheat flags before this was handled.
    GRP80 = {0: "rmw:add", 1: "rmw:or", 2: "rmw:adc", 3: "rmw:sbb",
             4: "rmw:and", 5: "rmw:sub", 6: "rmw:xor", 7: "cmp"}
    EXT = {(0xC6, 0): "WRITE", (0xF6, 0): "test"}
    ANY = {0x8A: "read", 0x88: "WRITE", 0x38: "cmp", 0x3A: "cmp", 0x84: "test"}

    def decode(seg, j, rnum):
        """Return (kind, imm) if seg[j:] is a byte access at [rnum+field]."""
        if j + 2 >= len(seg):
            return None
        op, modrm = seg[j], seg[j + 1]
        mod, regf, rm = modrm >> 6, (modrm >> 3) & 7, modrm & 7
        if rm != rnum:
            return None
        if mod == 1:
            disp, dlen = seg[j + 2], 1
        elif mod == 2 and j + 5 < len(seg):
            disp, dlen = struct.unpack_from("<I", seg, j + 2)[0], 4
        elif mod == 0 and rm != 5:
            disp, dlen = 0, 0
        else:
            return None
        if disp != field:
            return None
        kind = (GRP80.get(regf) if op == 0x80 else None) \
               or EXT.get((op, regf)) or ANY.get(op)
        if kind is None:
            return None
        imm = None
        if op in (0x80, 0xC6, 0xF6) and j + 2 + dlen < len(seg):
            imm = seg[j + 2 + dlen]
        return kind, imm

    loads = []
    o = t["off"]
    end = t["off"] + t["size"]
    while o < end - 6:
        reg = None
        ln = 0
        if b[o] == 0xA1 and b[o + 1:o + 5] == p:
            reg, ln = "eax", 5                                   # mov eax,[imm32]
        elif b[o] == 0x8B and (b[o + 1] & 0xC7) == 0x05 and b[o + 2:o + 6] == p:
            reg, ln = X86_REGS[(b[o + 1] >> 3) & 7], 6           # mov r32,[imm32]
        if reg:
            loads.append((o + base, o, ln, reg))
            o += ln
            continue
        o += 1

    print(f"{len(loads)} direct loads of {ptr:#x}{elf.gtag(ptr)}"
          + (f"; byte field +{field:#x}:" if field is not None else ""))
    if field is None:
        for va, _, _, reg in loads:
            print(f"  {va:#010x}{elf.gtag(va)}  -> {reg}   in {elf.label(va) or '?'}")
        # A global is not always a pointer that gets loaded. Flags like the
        # cheat bytes are touched by absolute addressing and produce zero
        # "loads" — reporting only the pointer form would read as "unused".
        abs_hits = set()
        o, endo = t["off"], t["off"] + t["size"]
        while o < endo - 6:
            op = b[o]
            kind = ilen = None
            # Absolute addressing is mod=00 rm=101; the reg field carries the
            # opcode extension, so the modrm byte is 0x05|(ext<<3) — 0x35 for
            # /6 (xor), not just 0x05 and 0x3D.
            if op in (0x80, 0xC6, 0xF6) and (b[o + 1] & 0xC7) == 0x05 and b[o + 2:o + 6] == p:
                regf = (b[o + 1] >> 3) & 7
                kind = {0x80: GRP80.get(regf),
                        0xC6: "WRITE" if regf == 0 else None,
                        0xF6: "test" if regf == 0 else None}[op]
                ilen = 7
                imm = b[o + 6] if kind else None
            elif op == 0xA0 and b[o + 1:o + 5] == p:
                kind, ilen, imm = "read", 5, None          # mov al, [addr]
            elif op == 0xA2 and b[o + 1:o + 5] == p:
                kind, ilen, imm = "WRITE", 5, None         # mov [addr], al
            elif op in (0x8A, 0x88) and (b[o + 1] & 0xC7) == 0x05 and b[o + 2:o + 6] == p:
                kind, ilen, imm = ("read" if op == 0x8A else "WRITE"), 6, None
            if kind:
                abs_hits.add((o + base, kind, imm))
                o += ilen
                continue
            o += 1
        if abs_hits:
            k = {}
            for _, kind, _ in abs_hits:
                k[kind] = k.get(kind, 0) + 1
            print(f"\n  {len(abs_hits)} absolute byte accesses to {ptr:#x} itself"
                  f"  ({', '.join(f'{v} {n}' for n, v in sorted(k.items()))})")
            for va, kind, imm in sorted(abs_hits):
                extra = f" = {imm}" if imm is not None else ""
                print(f"  {va:#010x}{elf.gtag(va)}  {kind:5} byte [{ptr:#x}]{extra}"
                      f"   in {elf.label(va) or '?'}")
        return

    rnum_of = {v: k for k, v in X86_REGS.items()}
    # Deduplicate before counting: two consecutive loads of the same global can
    # both reach the same access within the window, which double-counts it in
    # the summary while the listing (a set) shows it once — a header that
    # disagrees with its own list.
    found = set()
    for va, o, ln, reg in loads:
        seg = b[o + ln:o + ln + win]
        rnum = rnum_of[reg]
        for j in range(len(seg) - 2):
            got = decode(seg, j, rnum)
            if got is None:
                continue
            kind, imm = got
            found.add((va + ln + j, kind, reg, imm))
            break                     # first access after the load wins
    kinds = {}
    for _, kind, _, _ in found:
        kinds[kind] = kinds.get(kind, 0) + 1
    summary = ", ".join(f"{v} {k}" for k, v in sorted(kinds.items()))
    print(f"  {len(found)} accesses  ({summary})")
    for ava, kind, reg, imm in sorted(found):
        extra = f" = {imm}" if imm is not None else ""
        print(f"  {ava:#010x}{elf.gtag(ava)}  {kind:5} [{reg}+{field:#x}]{extra}"
              f"   in {elf.label(ava) or '?'}")


def v_vtable(elf, args):
    """Dump words with relocations applied.

    Deliberately a *dumper*, not an interpreter: g++ 2.x emits vtables either as
    plain pointer arrays (4-byte slots) or as {short delta; short index; void
    *pfn} structs (8-byte slots, pfn at +4), depending on -fvtable-thunks. These
    two binaries do not agree — libmvos reads as 8-byte entries, theocracy.real
    as 4-byte — so assuming a stride is how you end up reading a delta as a
    function pointer. Offsets are printed raw; a virtual call through `vt+0xc`
    means the word labelled +0x00c, whatever the stride happens to be.
    """
    if not args:
        sys.exit("vtable: need an address or symbol")
    va = parse_addr(elf, args[0])
    n = 12
    if "-n" in args:
        n = int(args[args.index("-n") + 1], 0)
    rel = elf.reloc_map()
    dem = demangle_map()
    print(f"vtable {va:#x}{elf.gtag(va)}  ({n} words)")
    for i in range(n):
        wa = va + i * 4
        raw = elf.word(wa)
        if raw is None:
            print(f"  +{i*4:#05x}  <not in a file-backed section>")
            continue
        note = ""
        r = rel.get(wa)
        if r:
            typ, si, sec = r
            s = elf.sym_by_index(si)
            nm = s["name"] if s else f"sym#{si}"
            note = f"{RELOC_TYPES.get(typ, typ)} -> {nm}   [{sec}]"
            if s and s["name"] in dem:
                note += f"\n{'':>12}{dem[s['name']]}"
        elif raw:
            lab = elf.label(raw)
            txt = elf.cstr(raw)
            if txt is not None and len(txt) > 2:
                note = f'-> "{txt}"'
            elif lab:
                note = f"-> {lab}"
        print(f"  +{i*4:#05x}  {raw:#010x}  {note}")


def v_strrefs(elf, args):
    if len(args) < 2:
        sys.exit("strrefs: need <lo> <hi>")
    lo, hi = parse_addr(elf, args[0]), parse_addr(elf, args[1])
    b = elf.buf
    olo, ohi = elf.off(lo), elf.off(hi)
    if olo is None or ohi is None:
        sys.exit("range is not inside a file-backed section")
    seen = []
    o = olo
    while o < ohi - 5:
        imm = None
        if b[o] == 0x68:                                   # push imm32
            imm, = struct.unpack_from("<I", b, o + 1)
            ln = 5
        elif 0xB8 <= b[o] <= 0xBF:                         # mov r32, imm32
            imm, = struct.unpack_from("<I", b, o + 1)
            ln = 5
        else:
            o += 1
            continue
        s = elf.cstr(imm)
        if s is not None and len(s) >= 2:
            seen.append((lo + (o - olo), imm, s))
        o += ln
    print(f"{len(seen)} string references in {lo:#x}..{hi:#x}{elf.gtag(lo)}")
    for va, imm, s in seen:
        print(f"  {va:#010x}  {imm:#010x}  {s!r}")


VERBS = {
    "sections": v_sections, "sym": v_sym, "plt": v_plt,
    "xref-call": v_xref_call, "xref-global": v_xref_global,
    "vtable": v_vtable, "strrefs": v_strrefs,
}


def main(argv):
    if len(argv) < 3 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0 if len(argv) > 1 else 2
    elf = Elf(resolve_image(argv[1]))
    verb = argv[2]
    if verb not in VERBS:
        sys.exit(f"unknown verb {verb!r}; one of: {', '.join(sorted(VERBS))}")
    VERBS[verb](elf, argv[3:])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
