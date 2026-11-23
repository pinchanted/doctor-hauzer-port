#!/usr/bin/env python3
"""Static analysis helper for 3DO ARM AIF executables (big-endian ARM60, image base 0).

usage: armanalyze.py launchme [--funcs] [--strings] [--dis ADDR [ADDR...]] [--xref TEXT]

- Finds functions (BL targets + APCS prologues), names SDK functions from Norcroft's embedded
  0xFF0000nn name markers, and resolves PC-relative literal loads / ADR to strings.
- --funcs: one line per function: address, size, embedded name, strings it references, callees.
- --dis: disassemble the functions containing the given addresses, with string/literal comments.
- --xref: list functions referencing strings that contain TEXT.
"""
import argparse, bisect, re, struct, sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_BIG_ENDIAN


class Image:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        w = struct.unpack_from('>16I', self.d, 0)
        self.ro_size, self.rw_size, self.zi_size = w[5], w[6], w[8]
        self.code_end = self.ro_size
        self.md = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_BIG_ENDIAN)
        self.md.skipdata = True
        self.ins = {}
        for i in self.md.disasm(self.d[:self.code_end], 0):
            self.ins[i.address] = i
        self.names = self._embedded_names()
        self.strings = self._strings()
        self._find_functions()

    def word(self, a):
        return struct.unpack_from('>I', self.d, a)[0]

    def _embedded_names(self):
        names = {}
        for i in range(0, self.code_end, 4):
            w = self.word(i)
            if (w & 0xFFFFFF00) == 0xFF000000 and 4 <= (w & 0xFF) <= 64 and (w & 3) == 0:
                n = w & 0xFF
                s = self.d[i - n:i].split(b'\0')[0]
                if s and all(32 < c < 127 for c in s):
                    names[i + 4] = s.decode()
        return names

    def _strings(self):
        out = {}
        for m in re.finditer(rb'[\x20-\x7e\x0a\x0d\x09]{4,}\x00', self.d):
            out[m.start()] = m.group()[:-1].decode('latin-1')
        return out

    def string_at(self, a):
        if a in self.strings:
            return self.strings[a]
        if 0 <= a < len(self.d):
            s = self.d[a:a + 80].split(b'\0')[0]
            if len(s) >= 4 and all(9 <= c < 127 for c in s):
                return s.decode('latin-1')
        return None

    def lit_ref(self, i):
        """Address loaded/computed PC-relative by instruction i, or None."""
        ops = i.op_str
        if i.mnemonic.startswith('ldr') and '[pc' in ops:
            m = re.search(r'#(-?0x[0-9a-f]+|-?\d+)\]', ops)
            off = int(m.group(1), 0) if m else 0
            la = i.address + 8 + off
            if 0 <= la < len(self.d) - 3:
                return self.word(la)
        base = i.mnemonic[:3]
        if base in ('add', 'adr', 'sub') and ', pc, #' in ops:
            m = re.search(r'pc, #(-?0x[0-9a-f]+|-?\d+)(?:, #(\d+))?$', ops)
            if not m:
                return None
            imm = int(m.group(1), 0)
            if m.group(2):  # capstone prints rotated immediates as "#imm, #rot"
                rot = int(m.group(2)) & 31
                imm = ((imm >> rot) | (imm << (32 - rot))) & 0xFFFFFFFF
            return i.address + 8 + (-imm if base == 'sub' else imm)
        return None

    def _find_functions(self):
        starts = set(self.names)
        self.calls = {}
        for a, i in self.ins.items():
            if i.mnemonic == 'bl':
                try:
                    t = int(i.op_str.lstrip('#'), 0)
                    if 0 <= t < self.code_end:
                        starts.add(t); self.calls.setdefault(a, t)
                except ValueError:
                    pass
            # APCS prologue: mov ip, sp ; stmdb sp!, {..., fp, ip, lr, pc}
            if i.mnemonic == 'mov' and i.op_str == 'ip, sp':
                n = self.ins.get(a + 4)
                if n and n.mnemonic in ('stmdb', 'stmfd', 'push') and 'lr' in n.op_str:
                    starts.add(a)
        self.funcs = sorted(s for s in starts if s in self.ins)

    def func_of(self, a):
        k = bisect.bisect_right(self.funcs, a) - 1
        return self.funcs[k] if k >= 0 else None

    def func_end(self, f):
        k = self.funcs.index(f)
        return self.funcs[k + 1] if k + 1 < len(self.funcs) else self.code_end

    def name(self, f):
        return self.names.get(f, f'sub_{f:05x}')

    def refs(self, f):
        strs, callees = [], []
        for a in range(f, self.func_end(f), 4):
            i = self.ins.get(a)
            if not i:
                continue
            r = self.lit_ref(i)
            if r is not None:
                s = self.string_at(r)
                if s:
                    strs.append(s)
            if a in self.calls:
                callees.append(self.calls[a])
        return strs, callees

    def disasm(self, f):
        out = []
        for a in range(f, self.func_end(f), 4):
            i = self.ins.get(a)
            if not i:
                out.append(f'{a:06x}: .word {self.word(a):#010x}')
                continue
            c = ''
            if a in self.calls:
                c = '  ; ' + self.name(self.calls[a])
            r = self.lit_ref(i)
            if r is not None:
                s = self.string_at(r)
                c = f'  ; ={r:#x}' + (f' "{s[:60]}"' if s else '')
            out.append(f'{a:06x}: {i.mnemonic:8} {i.op_str}{c}')
        return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('--funcs', action='store_true')
    ap.add_argument('--dis', nargs='*', default=[])
    ap.add_argument('--xref', default=None)
    a = ap.parse_args()
    img = Image(a.exe)
    if a.funcs:
        for f in img.funcs:
            strs, callees = img.refs(f)
            print(f'{f:06x} {img.func_end(f) - f:6d} {img.name(f):24s} '
                  + ' | '.join(repr(s[:40]) for s in strs[:6])
                  + ('   -> ' + ' '.join(img.name(c) for c in sorted(set(callees))[:8]) if callees else ''))
    if a.xref:
        for f in img.funcs:
            strs, _ = img.refs(f)
            hits = [s for s in strs if a.xref.lower() in s.lower()]
            if hits:
                print(f'{f:06x} {img.name(f)}: ' + ' | '.join(repr(h[:60]) for h in hits))
    for x in a.dis:
        f = img.func_of(int(x, 16))
        print(f'==== {img.name(f)} ({f:#x}..{img.func_end(f):#x})')
        print(img.disasm(f))


if __name__ == '__main__':
    main()
