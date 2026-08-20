#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Adam Bogos
"""GNU v2 (g++ 2.x) C++ symbol demangler.

Targeted at libmvos.so (Theocracy). Modern c++filt dropped v2 support, so this
reimplements the subset of the old libiberty cplus-dem.c grammar that this binary
actually uses. Coverage is measured against the full export list; anything that
doesn't fully consume is reported so the grammar can be extended.
"""
import sys, re

OPS = {
    'nw':' new','dl':' delete','vn':' new[]','vd':' delete[]','as':'=','eq':'==',
    'ne':'!=','lt':'<','gt':'>','le':'<=','ge':'>=','pl':'+','mi':'-','ml':'*',
    'dv':'/','md':'%','apl':'+=','ami':'-=','amu':'*=','adv':'/=','amd':'%=',
    'ls':'<<','rs':'>>','als':'<<=','ars':'>>=','or':'|','an':'&','er':'^',
    'aor':'|=','aan':'&=','aer':'^=','co':'~','nt':'!','aa':'&&','oo':'||',
    'pp':'++','mm':'--','cl':'()','vc':'[]','cm':',','rf':'->','ad':'&',
    'rm':'->*','pt':'->','sz':'sizeof',
}
BUILTIN = {
    'v':'void','i':'int','l':'long','s':'short','c':'char','w':'wchar_t',
    'b':'bool','f':'float','d':'double','r':'long double','x':'long long',
    'e':'...',
}

class Fail(Exception): pass

class Dem:
    def __init__(self, s):
        self.s = s; self.i = 0
    def peek(self): return self.s[self.i] if self.i < len(self.s) else ''
    def eof(self): return self.i >= len(self.s)

    def count(self):
        # decimal length prefix (name lengths / template arg counts)
        if self.peek() == '_':   # _<n>_ form for >=10 in some positions
            self.i += 1; j = self.s.index('_', self.i)
            n = int(self.s[self.i:j]); self.i = j+1; return n
        j = self.i
        while self.i < len(self.s) and self.s[self.i].isdigit(): self.i += 1
        if j == self.i: raise Fail("expected count")
        return int(self.s[j:self.i])

    def name(self):
        n = self.count()
        if self.i + n > len(self.s): raise Fail("name overrun")
        r = self.s[self.i:self.i+n]; self.i += n; return r

    def template(self):
        # 't' already consumed by caller OR handle here: expects at cursor a 't'
        assert self.s[self.i] == 't'; self.i += 1
        base = self.name()
        argc = self.count()
        args = []
        for _ in range(argc):
            c = self.peek()
            if c == 'Z':
                self.i += 1; args.append(self.type())
            else:
                # non-type (value) template arg: <type><value>  (best effort)
                t = self.type()
                # a literal usually follows as a signed decimal
                m = re.match(r'-?\d+', self.s[self.i:])
                if m:
                    args.append(m.group(0)); self.i += len(m.group(0))
                else:
                    args.append(t)
        return f"{base}<{', '.join(args)}>"

    def qname(self):
        # 'Q' already at cursor. Component count is a SINGLE digit, or _<n>_ for >=10.
        assert self.s[self.i] == 'Q'; self.i += 1
        if self.peek() == '_':
            self.i += 1; j = self.s.index('_', self.i)
            n = int(self.s[self.i:j]); self.i = j+1
        else:
            n = int(self.peek()); self.i += 1
        parts = []
        for _ in range(n):
            parts.append(self.classspec())
        return '::'.join(parts)

    def classspec(self):
        c = self.peek()
        if c == 'Q': return self.qname()
        if c == 't': return self.template()
        if c.isdigit() or c == '_': return self.name()
        raise Fail(f"bad classspec at {self.s[self.i:]!r}")

    def type(self, arglist=None):
        pref = []           # list of ('P'|'R'|'A'n|'F'..)
        sign = None
        while True:
            c = self.peek()
            if c == 'P': pref.append('*'); self.i += 1
            elif c == 'R': pref.append('&'); self.i += 1
            elif c == 'C': pref.append('const'); self.i += 1
            elif c == 'V': pref.append('volatile'); self.i += 1
            elif c == 'u': pref.append('restrict'); self.i += 1
            elif c == 'G': self.i += 1  # value-type marker: ignore
            elif c == 'U': sign = 'unsigned'; self.i += 1
            elif c == 'S': sign = 'signed'; self.i += 1
            elif c == 'A':
                self.i += 1; n = self.count()
                if self.peek() == '_': self.i += 1
                pref.append(f'[{n}]')
            elif c == 'F':
                # function type: F<args>_<ret>
                self.i += 1
                fargs = []
                while self.peek() not in ('_',) and not self.eof():
                    fargs.append(self.type())
                if self.peek() == '_': self.i += 1
                ret = self.type()
                base = f"{ret} (*)({', '.join(fargs)})"
                return self._wrap(base, pref, skip_ptr_space=True)
            elif c == 'M':
                self.i += 1; cls = self.classspec(); inner = self.type()
                base = f"{inner} {cls}::*"
                return self._wrap(base, pref)
            elif c == 'T':
                # backref to Nth already-seen arg type
                self.i += 1; k = int(self.s[self.i]); self.i += 1
                if arglist is None or k-1 >= len(arglist): raise Fail("bad T backref")
                base = arglist[k-1]
                return self._wrap(base, pref)
            else:
                break
        c = self.peek()
        if c in BUILTIN:
            self.i += 1
            base = BUILTIN[c]
            if sign: base = f"{sign} {base}" if base != 'int' or sign=='unsigned' else f"{sign} int"
            if sign == 'unsigned' and c == 'i': base = 'unsigned int'
        elif c.isdigit() or c in ('Q','t','_'):
            base = self.classspec()
            if sign: base = f"{sign} {base}"
        else:
            raise Fail(f"bad type at {self.s[self.i:]!r}")
        return self._wrap(base, pref)

    def _wrap(self, base, pref, skip_ptr_space=False):
        # apply qualifiers innermost(last)->outermost(first)
        s = base
        for q in reversed(pref):
            if q == 'const': s = f"const {s}"
            elif q == 'volatile': s = f"volatile {s}"
            elif q == 'restrict': s = f"{s} restrict"
            elif q == '*': s = s + '*' if s.endswith('*') else s + ' *'
            elif q == '&': s = s + ' &'
            elif q.startswith('['): s = f"{s} {q}"
        return s

    def args(self, cls=None):
        # cls seeds backref slot 0 (member-function 'T0'/'N.0' = the class type itself)
        out = []
        def backref(k):
            return cls if (k == 0 and cls is not None) else out[k-1]
        while not self.eof():
            if self.peek() == 'N':      # repeat: N<count><index>
                self.i += 1
                cnt = int(self.s[self.i]); idx = int(self.s[self.i+1]); self.i += 2
                for _ in range(cnt): out.append(backref(idx))
                continue
            if self.peek() == 'T':      # single backref
                self.i += 1; k = int(self.s[self.i]); self.i += 1
                out.append(backref(k)); continue
            out.append(self.type(out))
        if out == ['void']: return []
        return out


def demangle_type_or_class(s):
    d = Dem(s); r = d.classspec() if (s and (s[0].isdigit() or s[0] in 'Qt_')) else Dem(s).type()
    return r

def split_mixin(rest):
    return rest.split('.')

def demangle(sym):
    # data / plain C symbols
    if sym.startswith('__vt_'):
        parts = split_mixin(sym[5:])
        base = "::".join(dm_class(p) for p in parts[:1]) if False else dm_class(parts[0])
        extra = f" (base {dm_class(parts[1])})" if len(parts) > 1 else ""
        return f"{base} virtual table{extra}"
    if sym.startswith('_._'):
        cls = dm_class(sym[3:]); last = cls.split('::')[-1].split('<')[0]
        return f"{cls}::~{last}()"
    if sym.startswith('__ti'):
        return f"{dm_class(sym[4:])} type_info node"
    if sym.startswith('__tf'):
        return f"{dm_class(sym[4:])} type_info function"
    if sym.startswith('__thunk_'):
        m = re.match(r'__thunk_(-?\d+)_(.*)', sym)
        if m: return f"[thunk {m.group(1)}] " + demangle(m.group(2))
    if sym.startswith('__'):
        body = sym[2:]
        # operator?  __<opcode>__<class>...   or global __<opcode>__F...
        mop = match_operator(body)
        if mop: return mop
        # constructor: __<classspec><args>
        try:
            d = Dem(body); cls = d.classspec(); a = d.args(cls)
            last = cls.split('::')[-1].split('<')[0]
            return f"{cls}::{last}({', '.join(a)})"
        except Fail:
            pass
    # member / global:  name__[C][V]<classspec><args>   or  name__F<args>
    m = find_method_split(sym)
    if m: return m
    # plain symbol (no mangling) -> as-is (C-linkage global/object)
    return sym

def dm_class(s):
    try:
        return demangle_type_or_class(s)
    except Fail:
        return s

def match_operator(body):
    # conversion operator: op<type>__<class>
    if body.startswith('op'):
        idx = body.find('__')
        if idx > 2:
            tstr = body[2:idx]; cls_and = body[idx+2:]
            try:
                t = Dem(tstr).type()
                d = Dem(cls_and); cls = d.classspec(); a = d.args(cls)
                return f"{cls}::operator {t}({', '.join(a)})"
            except Fail: return None
    for L in (3,2):
        code = body[:L]
        if code in OPS and body[L:L+2] == '__':
            rest = body[L+2:]
            try:
                if rest.startswith('F'):
                    d = Dem(rest[1:]); a = d.args()
                    return f"operator{OPS[code]}({', '.join(a)})"
                d = Dem(rest); cls = d.classspec(); a = d.args(cls)
                return f"{cls}::operator{OPS[code]}({', '.join(a)})"
            except Fail: return None
    return None

def find_method_split(sym):
    # locate separator '__' dividing method name from signature
    start = 0
    while True:
        idx = sym.find('__', start)
        if idx < 0: return None
        name = sym[:idx]; rest = sym[idx+2:]
        if not name: start = idx+1; continue
        try:
            if rest.startswith('F'):
                d = Dem(rest[1:]); a = d.args()
                if d.eof(): return f"{name}({', '.join(a)})"
            else:
                d = Dem(rest)
                cv = ''
                # cv-qualified member fn: name__[C][V]<classspec>...
                while d.peek() in ('C', 'V') and (d.i+1 < len(rest) and (rest[d.i+1].isdigit() or rest[d.i+1] in 'Qt')):
                    cv += ' const' if d.peek() == 'C' else ' volatile'; d.i += 1
                cls = d.classspec(); a = d.args(cls)
                if d.eof():
                    return f"{cls}::{name}({', '.join(a)}){cv}"
        except (Fail, IndexError, ValueError):
            pass
        start = idx + 1

def looks_resolved(sym, dm):
    """Heuristic: did we fully demangle? (used only for the coverage stat)"""
    if re.match(r'^(__bss_start|__builtin_)', sym):
        return True  # genuine C runtime symbols, correctly passed through
    resolved = (dm != sym) or not re.match(r'^(__|_\._)', sym)
    return bool(resolved and not re.search(r'__\d|[A-Za-z]__[A-Z0-9]', dm.replace('operator', '')))


def main(argv):
    """Read `[addr] symbol` lines from stdin, write `addr<TAB>symbol<TAB>demangled`
    to stdout, and a coverage summary to stderr. Addresses are optional and passed
    through untouched (so `objdump -T` output and bare symbol lists both work)."""
    total = ok = 0
    fails = []
    for line in sys.stdin:
        line = line.rstrip('\n')
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) == 2 and re.match(r'^[0-9a-fA-F]+$', parts[0]):
            addr, sym = parts
        else:
            addr, sym = '', parts[0]
        total += 1
        try:
            dm = demangle(sym)
        except Exception as e:  # never abort the batch on one bad symbol
            dm = sym
            fails.append((sym, repr(e)))
        if looks_resolved(sym, dm):
            ok += 1
        elif dm == sym and re.match(r'^(__|_\._)', sym):
            fails.append((sym, 'unchanged'))
        sys.stdout.write(f"{addr}\t{sym}\t{dm}\n")
    sys.stderr.write(f"total={total} resolved={ok} ({100*ok//max(total,1)}%) flagged={len(fails)}\n")
    for s, e in fails[:40]:
        sys.stderr.write(f"  {s}\t{e}\n")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
