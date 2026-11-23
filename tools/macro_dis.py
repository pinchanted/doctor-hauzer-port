#!/usr/bin/env python3
"""Disassembler for Doctor Hauzer's room/event scripts (OrgData/macro/macroNNN, human/macroNNN).

usage: macro_dis.py <file> [<file> ...]   (writes a listing per file to stdout)
       macro_dis.py --dir <OrgData> --out <dir>   (all scripts -> <dir>/<name>.txt, plus opcode stats)

Format (reverse-engineered from launchme's VM at 0xa500; see docs/FORMATS.md):
a script is a stream of (tag, value) big-endian int32 pairs ("tokens"):
  tag 1 = integer   2 = label (token index; jump target = index * 2 words)
  tag 3 = local var (script context +0x10)   4 = opcode / comparator   5 = string char
  tag 6 = global var (game-wide table at 0x3d308)
The VM executes opcode tokens and skips everything else; assignments are infix
(<var> SET <expr>), so an opcode may consume the variable token just before it.
"""
import argparse, collections, os, struct, sys

# name, operands after the opcode, takes the variable before it
CORE = {
    0xfc01: ('BEGIN', 0, False),      # end of the DEF block; script starts running
    0xfc02: ('WAIT', 0, False),       # yield until the next frame
    0xfc03: ('GOTO', 1, False),
    0xfc04: ('PRINT', 1, False),      # debug print
    0xfc05: ('IF', 4, False),         # IF a <cmp> b label : jump when the comparison holds
    0xfc06: ('DEF', 2, False),        # DEF var = const (initialisation)
    0xfc08: ('ON', 2, False),         # ON event GOTO label
    0xfc09: ('CALL', 1, False),
    0xfc0a: ('RETURN', 0, False),
    0xfc0d: ('FOR', None, False),
    0xfc0e: ('NEXT', None, False),
    0xfc0f: ('MARK', 0, False),       # save resume point
    0xfc10: ('WHILE', None, False),
    0xfc11: ('ON3', 4, False),        # ON (type, object, param) GOTO label
    0xfcc1: ('FLAG_ON', None, False),
    0xfcc2: ('FLAG_OFF', None, False),
    0xfcc3: ('FLAG_3', None, False),
    0xfce0: ('=', 1, True),
    0xfce1: ('+=', 1, True),
    0xfce2: ('-=', 1, True),
    0xfce3: ('*=', 1, True),
    0xfce4: ('/=', 1, True),
    0xfce5: ('%=', 1, True),
    0xfce6: ('++', 0, True),
    0xfce7: ('--', 0, True),
}
# operand-token counts per opcode, read from each handler's script-counter increment in launchme
ARITY = {
    0xfc01: 0, 0xfc02: 0, 0xfc03: 1, 0xfc04: 1, 0xfc05: 4, 0xfc06: 2, 0xfc08: 2, 0xfc09: 1,
    0xfc0a: 0, 0xfc0d: 4, 0xfc0e: 0, 0xfc0f: 0, 0xfc10: 3, 0xfc11: 4, 0xfc60: 0, 0xfc61: 0,
    0xfc62: 0, 0xfc63: 0, 0xfc64: 1, 0xfc65: 0, 0xfc66: 2, 0xfc67: 2, 0xfc68: 2, 0xfc69: 2,
    0xfc6a: 1, 0xfc6b: 0, 0xfc6c: 0, 0xfc6d: 2, 0xfc70: 1, 0xfc71: None, 0xfc72: 3, 0xfc73: 1,
    0xfc74: 0, 0xfc75: 1, 0xfc76: 1, 0xfc77: 2, 0xfc78: 1, 0xfc79: 1, 0xfc7a: 0, 0xfc7b: 0,
    0xfc7c: 0, 0xfc7d: 1, 0xfc7e: 3, 0xfc7f: 1, 0xfc80: 0, 0xfc81: 0, 0xfc82: 0, 0xfc83: 2,
    0xfc84: 2, 0xfc85: 0, 0xfc86: 0, 0xfc87: 0, 0xfc88: 0, 0xfc89: 12, 0xfc8a: 0, 0xfc8b: 0,
    0xfc8c: 1, 0xfc8d: 2, 0xfc8e: 2, 0xfc8f: 2, 0xfc90: 3, 0xfc91: 0, 0xfc92: 0, 0xfc93: 2,
    0xfc94: 1, 0xfc95: 1, 0xfc96: 3, 0xfc97: 3, 0xfc98: 5, 0xfc99: 1, 0xfc9a: 11, 0xfc9b: 0,
    0xfc9c: 1, 0xfc9d: 0, 0xfc9e: 0, 0xfc9f: 1, 0xfca0: 4, 0xfca1: 4, 0xfca2: 1, 0xfca3: 4,
    0xfca4: 4, 0xfca5: 1, 0xfca6: 1, 0xfca7: 0, 0xfca8: 0, 0xfca9: 1, 0xfcaa: 3, 0xfcab: 0,
    0xfcac: 0, 0xfcad: 0, 0xfcae: 2, 0xfcaf: 0, 0xfcb0: 1, 0xfcb1: 1, 0xfcb2: 0, 0xfcb3: 1,
    0xfcb4: 1, 0xfcb5: 2, 0xfcb6: 2, 0xfcb7: 1, 0xfcb8: 1, 0xfcb9: 1, 0xfcba: 1, 0xfcbb: 1,
    0xfcbc: 1, 0xfcbd: 1, 0xfcbe: 1, 0xfcbf: 1, 0xfcc0: 2, 0xfcc1: 1, 0xfcc2: 1, 0xfcc3: 2,
    0xfcc4: 2, 0xfcc5: 0, 0xfcc6: 1, 0xfcc7: 3, 0xfcc8: 1, 0xfcc9: 2, 0xfcca: 1, 0xfccb: 0,
    0xfccc: 0, 0xfccd: 0, 0xfcce: 1, 0xfccf: 2, 0xfce0: 1, 0xfce1: 1, 0xfce2: 1, 0xfce3: 1,
    0xfce4: 1, 0xfce5: 1, 0xfce6: 0, 0xfce7: 0,
}
# opcodes whose handler reads the variable token just before them (result goes there)
LHS_OPS = {0xfc63, 0xfc64, 0xfc65, 0xfc7b, 0xfc7c, 0xfc8d, 0xfc8e, 0xfc8f, 0xfcb7, 0xfcb8, 0xfcb9, 0xfcba, 0xfcbb, 0xfcbc, 0xfce0, 0xfce1, 0xfce2, 0xfce3, 0xfce4, 0xfce5, 0xfce6, 0xfce7}
CMP = {0xfcd0: '>=', 0xfcd1: '==', 0xfcd2: '>', 0xfcd3: '<', 0xfcd4: '!=', 0xfcd5: '<='}


def tokens(data):
    n = len(data) // 8
    w = struct.unpack(f'>{n * 2}i', data[:n * 8])
    return list(zip(w[0::2], w[1::2]))


def fmt(tok):
    t, v = tok
    if t == 1:
        return str(v)
    if t == 2:
        return f'L{v}'
    if t == 3:
        return f'v{v}'
    if t == 6:
        return f'g{v}'
    if t == 4:
        return CMP.get(v, f'op_{v:x}')
    if t == 5:
        return repr(chr(v & 0xFF))
    return f'?{t}:{v}'


def disassemble(data):
    toks = tokens(data)
    labels = {v for t, v in toks if t == 2}
    lines, i, pending = [], 0, []
    while i < len(toks):
        t, v = toks[i]
        if i in labels:
            if pending:
                lines.append((None, '    ; skipped: ' + ' '.join(fmt(x) for x in pending))); pending = []
            lines.append((None, f'L{i}:'))
        if t != 4 or v in CMP:
            # a variable directly before an assignment opcode is its target; anything else here is inert
            pending.append(toks[i]); i += 1
            continue
        if v == 0:  # separator
            if pending:
                lines.append((None, '    ; skipped: ' + ' '.join(fmt(x) for x in pending)))
            pending = []; i += 1
            continue
        name, nargs, lhs = CORE.get(v, (f'op_{v:x}', None, False))
        if ARITY.get(v) is not None:
            nargs = ARITY[v]
        lhs = lhs or v in LHS_OPS
        j = i + 1
        if nargs is None:  # variable length: take operands up to the next real opcode/separator/label
            while j < len(toks) and not (toks[j][0] == 4 and toks[j][1] not in CMP) and j not in labels:
                j += 1
        else:
            j = min(len(toks), i + 1 + nargs)
        args = [fmt(x) for x in toks[i + 1:j]]
        target = pending.pop() if (lhs and pending) else None
        if pending:
            lines.append((None, '    ; skipped: ' + ' '.join(fmt(x) for x in pending))); pending = []
        if lhs and v not in CORE:
            text = f'{fmt(target) if target else "?"} = {name}({", ".join(args)})'
        elif lhs:
            text = f'{fmt(target) if target else "?"} {name} {" ".join(args)}'.rstrip()
        elif name == 'IF' and len(args) == 4:
            text = f'IF {args[0]} {args[1]} {args[2]} GOTO {args[3]}'
        elif name in ('ON', 'ON3') and args:
            text = f'ON ({", ".join(args[:-1])}) GOTO {args[-1]}'
        elif name == 'DEF' and len(args) == 2:
            text = f'DEF {args[0]} = {args[1]}'
        else:
            text = f'{name} {", ".join(args)}'.rstrip()
        lines.append((i, '    ' + text))
        i = j
    if pending:
        lines.append((None, '    ' + ' '.join(fmt(x) for x in pending)))
    return '\n'.join(text for _, text in lines), toks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='*')
    ap.add_argument('--dir'); ap.add_argument('--out')
    a = ap.parse_args()
    if a.dir:
        os.makedirs(a.out, exist_ok=True)
        stats = collections.Counter()
        paths = [os.path.join(a.dir, 'macro', f) for f in sorted(os.listdir(os.path.join(a.dir, 'macro')))]
        paths += [os.path.join(a.dir, 'human', f) for f in sorted(os.listdir(os.path.join(a.dir, 'human'))) if f.startswith('macro')]
        for p in paths:
            text, toks = disassemble(open(p, 'rb').read())
            name = os.path.basename(os.path.dirname(p)) + '_' + os.path.basename(p)
            open(os.path.join(a.out, name + '.txt'), 'w').write(f'; {p}  ({len(toks)} tokens)\n' + text + '\n')
            for t, v in toks:
                if t == 4 and v and v not in CMP:
                    stats[v] += 1
        with open(os.path.join(a.out, '_opcodes.txt'), 'w') as f:
            for v, c in stats.most_common():
                f.write(f'{v:#x} {CORE.get(v, (f"op_{v:x}",))[0]:10s} {c}\n')
        print(f'{len(paths)} scripts -> {a.out}')
    for p in a.files:
        print(disassemble(open(p, 'rb').read())[0])


if __name__ == '__main__':
    main()
