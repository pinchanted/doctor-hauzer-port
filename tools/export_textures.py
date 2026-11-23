#!/usr/bin/env python3
"""Export every texture in the game for upscaling.

usage: export_textures.py <OrgData dir> <out dir>

Writes, per unique texture (deduplicated by content hash):
  <hash>.png          as the original game shows it (RGBA, transparency preserved)
and textures.json mapping (bank, index) -> hash plus size/format info.
"""
import hashlib, json, os, struct, sys

sys.path.insert(0, os.path.dirname(__file__))
from cel import Cel, to_image


def banks(org):
    for fn in sorted(os.listdir(os.path.join(org, 'room'))):
        p = os.path.join(org, 'room', fn)
        d = open(p, 'rb').read()
        yield fn[:-4], d, struct.unpack_from('>I', d, 0)[0]
    for sub, fn in (('human', 'HumanTex.bin'), ('item', 'ItemTex.bin')):
        d = open(os.path.join(org, sub, fn), 'rb').read()
        yield sub, d, 0


def main(org, out):
    os.makedirs(out, exist_ok=True)
    index, seen = [], {}
    for name, d, base in banks(org):
        n = struct.unpack_from('>I', d, base)[0]
        for i in range(n):
            o, s = struct.unpack_from('>II', d, base + 4 + 8 * i)
            raw = d[base + o:base + o + s]
            h = hashlib.sha1(raw).hexdigest()[:16]
            entry = {'bank': name, 'index': i, 'hash': h}
            if h not in seen:
                try:
                    c = Cel(raw)
                    info = {'w': c.width, 'h': c.height, 'bpp': c.bpp, 'packed': c.packed,
                            'uncoded': c.uncoded, 'amv': c.amv_shade}
                    to_image(c).save(os.path.join(out, h + '.png'))
                except Exception as e:  # keep going; report at the end
                    info = {'error': str(e)}
                seen[h] = info
            entry.update(seen[h])
            index.append(entry)
    json.dump(index, open(os.path.join(out, 'textures.json'), 'w'), indent=1)
    errs = [h for h, v in seen.items() if 'error' in v]
    print(f'{len(index)} texture refs, {len(seen)} unique, {len(errs)} errors')
    by = {}
    for v in seen.values():
        k = (v.get('w'), v.get('h'), v.get('bpp'), v.get('amv'))
        by[k] = by.get(k, 0) + 1
    for k, c in sorted(by.items(), key=lambda kv: -kv[1]):
        print('  %s: %d' % (k, c))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
