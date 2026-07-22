#!/usr/bin/env python3
"""Aggregate demangled GNU-v2 symbols into a structured API inventory.

Input : TSV lines `addr<TAB>rawsymbol<TAB>demangled` (from gnuv2_demangle.py).
Output: JSON on stdout — { classes: {name: {methods:[...], ctors, dtor, vtable,
        rtti, operators, statics}}, globals: [...], free_functions: [...] }.

A "class" is the scope before `::` in a demangled name. Free functions and plain
data symbols (singletons like VVC/EnvSystem) are bucketed separately.
"""
import sys, re, json
from collections import defaultdict

def scope_and_member(dm):
    """Split 'A::B::method(args)' -> ('A::B', 'method(args)'). Respects <...> and (...)."""
    depth = 0; last = -1; i = 0
    while i < len(dm):
        c = dm[i]
        if c in '<(': depth += 1
        elif c in '>)': depth -= 1
        elif c == ':' and depth == 0 and dm[i:i+2] == '::':
            last = i; i += 2; continue
        i += 1
    if last < 0:
        return None, dm
    return dm[:last], dm[last+2:]

def classify(raw, dm, scope, member):
    if raw.startswith('__vt_'): return 'vtable'
    if 'type_info node' in dm: return 'rtti_node'
    if 'type_info function' in dm: return 'rtti_func'
    if member.startswith('~'): return 'dtor'
    if 'operator' in member: return 'operator'
    if scope:
        base = scope.split('::')[-1].split('<')[0]
        if member.split('(')[0].split('<')[0] == base: return 'ctor'
    return 'method'

def main():
    classes = defaultdict(lambda: defaultdict(list))
    free = []
    data = []
    RTTI_SUF = {' virtual table': 'vtable', ' type_info node': 'rtti_node',
                ' type_info function': 'rtti_func'}
    for line in sys.stdin:
        line = line.rstrip('\n')
        if not line: continue
        f = line.split('\t')
        addr = f[0] if len(f) == 3 else ''
        raw = f[-2]; dm = f[-1]
        # g++ file-scope static ctor/dtor thunks (_GLOBAL_.I./.D.) — not real methods
        if raw.startswith('_GLOBAL_') or raw.startswith('_._GLOBAL'):
            data.append({'addr': addr, 'raw': raw, 'name': '(static init/fini) ' + raw})
            continue
        # vtable / type_info entries: attribute to the owning class, not to (free)
        rtti_kind = next((k for suf, k in RTTI_SUF.items() if suf in dm), None)
        if rtti_kind:
            owner = dm
            for suf in RTTI_SUF:
                owner = owner.split(suf)[0]
            owner = owner.split(' (base')[0].strip()   # drop "(base X)" mixin note
            classes[owner][rtti_kind].append({'addr': addr, 'raw': raw, 'sig': dm, 'member': ''})
            continue
        if '(' not in dm and '::' not in dm:
            # plain data symbol / unmangled C global (VVC, EnvSystem, LFB blitters, libc)
            (data if re.match(r'^[A-Za-z_]\w*$', raw) else free).append({'addr': addr, 'raw': raw, 'name': dm})
            continue
        scope, member = scope_and_member(dm)
        kind = classify(raw, dm, scope, member)
        if not scope:
            free.append({'addr': addr, 'raw': raw, 'sig': dm})
        else:
            classes[scope][kind].append({'addr': addr, 'raw': raw, 'sig': dm, 'member': member})
    # emit
    out = {'classes': {}, 'free_functions': free, 'data_globals': data}
    for cls in sorted(classes):
        buckets = classes[cls]
        out['classes'][cls] = {
            'method_count': len(buckets.get('method', [])),
            'ctors': [m['sig'] for m in buckets.get('ctor', [])],
            'dtor': [m['sig'] for m in buckets.get('dtor', [])],
            'methods': [m['sig'] for m in buckets.get('method', [])],
            'operators': [m['sig'] for m in buckets.get('operator', [])],
            'has_vtable': bool(buckets.get('vtable')),
            'has_rtti': bool(buckets.get('rtti_node') or buckets.get('rtti_func')),
            'addrs': {m['member'] if m['member'] else m['sig']: m['addr']
                      for k in ('ctor','dtor','method','operator') for m in buckets.get(k, []) if m['addr']},
        }
    json.dump(out, sys.stdout, indent=1)
    sys.stderr.write(f"classes={len(out['classes'])} free_fns={len(free)} data_globals={len(data)}\n")

if __name__ == '__main__':
    main()
