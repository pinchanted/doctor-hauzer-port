#!/usr/bin/env python3
"""Convert 3DO IMAG (.img) files (16bpp, linear or LRform) to PNG."""
import struct, sys, zlib, os

def write_png(path, w, h, rows):
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def rgb(p):
    r, g, b = (p >> 10) & 31, (p >> 5) & 31, p & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)

def cel_header(d):
    """A full-screen picture stored as a CEL (the English fan patch writes its stills this way):
    16-bit uncoded, unpacked, LRform (PRE1 bit 11). Returns (w, h, bytes per row pair, bpp, order)."""
    c = d.find(b'CCB '); assert c >= 0, 'no IMAG or CCB chunk'
    body = c + 8
    pre0, pre1, w, h = struct.unpack_from('>IIII', d, body + 56)
    bpp = {6: 16}.get(pre0 & 7, 0)
    lr = (pre1 >> 11) & 1
    return w, h, w * 4 if lr else w * 2, bpp, 1 if lr else 0

def convert(src, dst):
    d = open(src, 'rb').read()
    i = d.find(b'IMAG')
    if i >= 0:
        w, h, bpr = struct.unpack_from('>III', d, i + 8)
        bpp, ncomp, planar, cspace, comp, hv, order = d[i + 20:i + 27]
    else:
        w, h, bpr, bpp, order = cel_header(d)
    p = d.find(b'PDAT'); pix = d[p + 8:]
    assert bpp == 16, f'bpp {bpp}'
    rows = []
    if order == 1:  # LRform: each 32-bit word holds a pixel from an even and an odd line
        for y2 in range(0, h, 2):
            top, bot = [], []
            base = (y2 // 2) * w * 4
            for x in range(w):
                a, b = struct.unpack_from('>HH', pix, base + x * 4)
                top += rgb(a); bot += rgb(b)
            rows += [top, bot]
    else:
        for y in range(h):
            row = []
            for x in range(w): row += rgb(struct.unpack_from('>H', pix, y * bpr + x * 2)[0])
            rows.append(row)
    write_png(dst, w, h, rows[:h])
    return w, h, order

if __name__ == '__main__':
    src_root, dst_root = sys.argv[1], sys.argv[2]
    for root, _, files in os.walk(src_root):
        for fn in files:
            if fn.lower().endswith('.img') or fn in ('MADO3',):
                s = os.path.join(root, fn)
                d = os.path.join(dst_root, os.path.relpath(s, src_root)) + '.png'
                os.makedirs(os.path.dirname(d), exist_ok=True)
                try: print(os.path.relpath(s, src_root), *convert(s, d))
                except Exception as e: print('FAIL', s, e)
