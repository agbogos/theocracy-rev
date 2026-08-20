#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
"""Generate C++ declaration stubs (the HLE compile-time contract) from the API
inventory.  Inputs:
  data/mvos_api.json      (from build_api_inventory.py)
  data/mvos_exports.tsv   (for vtable-mixin base hints + addresses)
Output (stdout): a single self-contained header.

IMPORTANT LIMITATIONS (inherent to GNU-v2 mangling — documented in the banner):
 * Return types are NOT encoded in the symbols -> emitted as `/*ret*/` (grep to fill
   from Ghidra decompiles).
 * Which methods are virtual, and vtable slot order, are NOT recoverable from symbols
   -> classes are only flagged polymorphic; order must come from Ghidra vtable reads.
 * Object layouts (field offsets) are not in symbols -> spliced in only for the few
   classes we reverse-engineered by hand (see LAYOUTS below / docs/structs, docs/subsystems).
"""
import sys, re, json, collections

# hand-recovered layouts (offsets) from Ghidra ctor analysis — see docs/.
LAYOUTS = {
    'cNode': "// layout (0x0c): +0x00 next; +0x04 prev; +0x08 vtable  [AmigaOS Exec node]",
    'cMemBlock_': "// payload base of cMemBlock: {data, size, lockCount}; owns Lock/Unlock/GetAddress",
    'cMemBlock': ("// layout (0x20): +0x00 next; +0x04 prev; +0x08 vtable(cNode);\n"
                  "    //   +0x0c data; +0x10 size; +0x14 lockCount; +0x18 priority(=0x7f); +0x1c vtable(cMemBlock_)"),
    'cString': "// is-a tMemBlock<char> -> cMemBlock (0x20). Storage is a lockable/evictable managed block.",
    'cSystemMemory': "// +0x04 tList<cMemBlock>; +0x1c budget=0x2000000 (32MB); +0x20 remaining. Evicts first UNLOCKED block.",
    'cList': "// circular doubly-linked, two embedded sentinels (head +0x00, tail +0x0c) [AmigaOS MinList]",
    'cThread': "// +0x04 cStream vtable base; +0x08/+0x0c pipe fds; +0x10 running; +0x14 pthread_t. Launch=CreatePipe+pthread_create(Entry).",
}

def load():
    api = json.load(open('data/mvos_api.json'))
    bases = collections.defaultdict(list)
    addrs = {}
    for line in open('data/mvos_exports.tsv'):
        f = line.rstrip('\n').split('\t')
        if len(f) != 3: continue
        addr, raw, dm = f
        if raw.startswith('__vt_') and '.' in raw[5:]:
            m = re.match(r'(.+?) virtual table \(base (.+?)\)', dm)
            if m: bases[m.group(1)].append(m.group(2))
    return api, bases

def is_template(name): return '<' in name
def strip_scope(sig, scope):
    p = scope + '::'
    return sig[len(p):] if sig.startswith(p) else sig

def member_key(sig, scope):
    """key into the addrs map — matches how build_api_inventory stored it (member text)."""
    return strip_scope(sig, scope)

def decl_line(sig, scope, kind, addr):
    member = strip_scope(sig, scope)
    at = f"   // @{addr}" if addr else ""
    if kind in ('ctor', 'dtor'):
        return member + ';' + at                 # no return type
    if member.startswith('operator ') and '(' in member and member.rstrip().endswith(')') \
            and not any(member.startswith('operator'+o) for o in
                        ('=','=','+','-','*','/','%','<','>','!','&','|','^','~','[','(',',')):
        # conversion operator: 'operator char *()' — target type IS the return
        return member + ';' + at
    return 'mvret ' + member + ';' + at + '  /*ret?*/'

def emit_class(name, c, bases, out):
    tmpl = is_template(name)
    base_hint = bases.get(name, [])
    tag = []
    if c['has_vtable']: tag.append('polymorphic')
    if c['has_rtti']: tag.append('rtti')
    hdr = f"// ==== {name} " + (f"[{', '.join(tag)}] " if tag else "")
    hdr += "="*max(2, 66-len(hdr))
    out.append(hdr)
    if base_hint:
        out.append(f"//   bases (vtable-mixin hints, unordered/possibly-incomplete): {', '.join(base_hint)}")
    if tmpl:
        out.append(f"// template instantiation — body commented (needs the primary template def)")
    lay = LAYOUTS.get(name)
    kw = 'struct' if name.startswith('s') and not name.startswith('sc') else 'class'
    prefix = '// ' if tmpl else ''
    out.append(prefix + f"{kw} {name} {{")
    if lay: out.append(prefix + "    " + lay)
    out.append(prefix + "public:")
    addrs = c.get('addrs', {})
    seen = set()
    def add(sigs, kind):
        for s in sigs:
            addr = addrs.get(member_key(s, name), '')
            d = decl_line(s, name, kind, addr)
            base = d.split('//')[0].strip()
            if base in seen: continue
            seen.add(base)
            out.append(prefix + "    " + d)
    add(c['ctors'], 'ctor')
    add(c['dtor'], 'dtor')
    add(c['operators'], 'op')
    add(c['methods'], 'method')
    out.append(prefix + "};")
    out.append("")

def main():
    api, bases = load()
    out = []
    out.append("// ============================================================================")
    out.append("//  mvos_api.hpp  —  GENERATED signature reference for libmvos.so (Theocracy)")
    out.append("//  Regenerate:  python3 tools/gen_headers.py > include/mvos_api.hpp")
    out.append("//  Source:      data/mvos_api.json (see docs/reference/mvos-api-inventory.md)")
    out.append("//")
    out.append("//  WHAT THIS IS: the demangled method contract for every engine class, each")
    out.append("//  method tagged with its file address (@0x..., = Ghidra addr - 0x10000). Use it")
    out.append("//  as the HLE implementation worklist and signature map.")
    out.append("//")
    out.append("//  WHAT THIS IS NOT (yet): a standalone translation unit. Known gaps, all")
    out.append("//  inherent to the symbol table (fill in as you implement, from Ghidra):")
    out.append("//   * mvret /*ret?*/  = return type unknown (GNU-v2 mangling omits it).")
    out.append("//   * [polymorphic]   = has a vtable, but WHICH methods are virtual and their")
    out.append("//                       slot ORDER are not in the symbols — read Ghidra vtables.")
    out.append("//   * bases are vtable-mixin hints only (primary base + order not recoverable).")
    out.append("//   * template params (tPoint<long>, cArray<...>) and enums (eBMType, ...) are")
    out.append("//     referenced but not defined here; real defs come with the layout work.")
    out.append("//   * field layouts appear only for hand-reversed classes (see docs/structs).")
    out.append("// ============================================================================")
    out.append("#ifndef MVOS_API_HPP")
    out.append("#define MVOS_API_HPP")
    out.append("")
    out.append("typedef int mvret;   // placeholder for unknown return types (grep: /*ret?*/)")
    out.append("")
    classes = api['classes']
    normal = sorted(n for n in classes if not is_template(n))
    tmpls  = sorted(n for n in classes if is_template(n))
    # forward declarations
    out.append("// ---- forward declarations ----")
    for n in normal:
        kw = 'struct' if n.startswith('s') and not n.startswith('sc') else 'class'
        out.append(f"{kw} {n};")
    out.append("")
    out.append(f"// ==================== CLASSES ({len(normal)}) ====================")
    out.append("")
    for n in normal:
        emit_class(n, classes[n], bases, out)
    out.append(f"// ============ TEMPLATE INSTANTIATIONS ({len(tmpls)}) ============")
    out.append("// (commented — the underlying template definitions are not recovered)")
    out.append("")
    for n in tmpls:
        emit_class(n, classes[n], bases, out)
    # data singletons
    out.append(f"// ==================== DATA GLOBALS / SINGLETONS ====================")
    out.append("// engine singletons the game references via copy relocations, plus C blitters.")
    for g in api['data_globals']:
        if re.match(r'^[A-Za-z_]\w*$', g['name']):
            out.append(f"extern void* {g['name']};   // {g['addr']}")
    out.append("")
    out.append(f"// ==================== FREE FUNCTIONS ({len(api['free_functions'])}) ====================")
    for fn in api['free_functions']:
        sig = fn.get('sig') or fn.get('name')
        if sig and '(' in sig:
            out.append(f"/*ret*/ {sig};")
    out.append("")
    out.append("#endif // MVOS_API_HPP")
    sys.stdout.write('\n'.join(out) + '\n')
    sys.stderr.write(f"emitted {len(normal)} classes, {len(tmpls)} template insts, "
                     f"{len(api['free_functions'])} free fns\n")

if __name__ == '__main__':
    main()
