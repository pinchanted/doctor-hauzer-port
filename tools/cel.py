#!/usr/bin/env python3
"""3DO CEL decoder (CCB/PLUT/PDAT chunks) -> RGBA.

Supports 1/2/4/6/8/16 bpp, coded (PLUT) and uncoded, packed and unpacked,
plus 8-bit coded AMV shading (PIXC multiplier-select = AMV): pixel = PLUT[v & 31] * ((v >> 5) + 1) / 8.

Note: in Doctor Hauzer the 5-bit index and 3-bit AMV together act as a 256-colour
quantizer (32 palette entries x 8 brightness levels). AMV carries texture detail, not a
separate lighting layer, so always upscale the final decoded colour.
"""
import struct

BPP_TABLE = {1: 1, 2: 2, 3: 4, 4: 6, 5: 8, 6: 16}
F_PACKED = 1 << 9
F_BGND = 1 << 5


def rgb555(p):
    r, g, b = (p >> 10) & 31, (p >> 5) & 31, p & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


class BitReader:
    def __init__(self, data, pos=0):
        self.d, self.bit = data, pos * 8

    def read(self, n):
        v = 0
        for _ in range(n):
            byte = self.d[self.bit >> 3] if (self.bit >> 3) < len(self.d) else 0
            v = (v << 1) | ((byte >> (7 - (self.bit & 7))) & 1)
            self.bit += 1
        return v


def iter_chunks(data, start=0, end=None):
    end = len(data) if end is None else end
    off = start
    while off + 8 <= end:
        tag = data[off:off + 4]
        size = struct.unpack_from('>I', data, off + 4)[0]
        if size < 8:
            break
        yield tag, off, size
        off += (size + 3) & ~3


class Cel:
    def __init__(self, data, start=0, end=None):
        self.ccb = self.plut = self.pdat = None
        for tag, off, size in iter_chunks(data, start, end):
            body = data[off + 8:off + size]
            if tag == b'CCB ':
                self.ccb = body
            elif tag == b'PLUT':
                n = struct.unpack_from('>I', body, 0)[0]
                self.plut = [struct.unpack_from('>H', body, 4 + 2 * i)[0] for i in range(n)]
            elif tag == b'PDAT':
                self.pdat = body
        if self.ccb is None or self.pdat is None:
            raise ValueError('missing CCB or PDAT')
        c = self.ccb  # body starts at version word
        (self.version, self.flags) = struct.unpack_from('>II', c, 0)
        self.pixc = struct.unpack_from('>I', c, 52)[0]
        self.pre0, self.pre1, self.width, self.height = struct.unpack_from('>IIii', c, 56)
        self.bpp = BPP_TABLE.get(self.pre0 & 7, 0)
        self.uncoded = bool(self.pre0 & (1 << 4))
        self.packed = bool(self.flags & F_PACKED)
        self.bgnd = bool(self.flags & F_BGND)
        if not self.packed:
            self.height = ((self.pre0 >> 6) & 0x3FF) + 1
            self.width = (self.pre1 & 0x7FF) + 1
        # PIXC primary half: MS (bits 14-13) == 1 selects per-pixel AMV as multiplier
        self.amv_shade = ((self.pixc >> 13) & 3) == 1

    def _pixel(self, v):
        """Return (r,g,b,a) for a raw pixel value."""
        if self.bpp == 16:
            if v == 0 and not self.bgnd:
                return (0, 0, 0, 0)
            if self.uncoded:
                return rgb555(v) + (255,)
            return rgb555(self.plut[v & 31] if self.plut else v) + (255,)
        if self.uncoded and self.bpp == 8:  # 8-bit uncoded: 3-3-2
            r, g, b = (v >> 5) & 7, (v >> 2) & 7, v & 3
            return (r * 255 // 7, g * 255 // 7, b * 255 // 3, 255)
        idx = v & 31
        if self.plut is None or idx >= len(self.plut):
            return (v, v, v, 255)
        p = self.plut[idx]
        if p == 0 and not self.bgnd:
            return (0, 0, 0, 0)
        r, g, b = rgb555(p)
        if self.bpp == 8 and self.amv_shade:
            m = ((v >> 5) & 7) + 1
            r, g, b = r * m // 8, g * m // 8, b * m // 8
        return (r, g, b, 255)

    def decode(self, pix=None):
        """Return list of rows. pix maps a raw pixel value to the output value
        (default: RGBA tuple); transparent runs in packed data yield None."""
        pix = pix or self._pixel
        w, h, bpp, d = self.width, self.height, self.bpp, self.pdat
        rows = []
        if not self.packed:
            stride = ((self.pre1 >> 16) & (0x3FF if bpp >= 8 else 0xFF)) + 2
            stride *= 4
            for y in range(h):
                br = BitReader(d, y * stride)
                rows.append([pix(br.read(bpp)) for _ in range(w)])
            return rows
        pos = 0
        offw = 2 if bpp >= 8 else 1  # row-offset word size in bytes
        for y in range(h):
            if pos >= len(d):
                rows.append([None] * w)
                continue
            nxt = (d[pos] if offw == 1 else (d[pos] << 8 | d[pos + 1])) + 2
            br = BitReader(d, pos)
            br.read(offw * 8)
            row = []
            while len(row) < w:
                typ = br.read(2)
                if typ == 0:
                    break
                cnt = br.read(6) + 1
                if typ == 1:
                    row += [pix(br.read(bpp)) for _ in range(cnt)]
                elif typ == 2:
                    row += [None] * cnt
                else:
                    px = pix(br.read(bpp))
                    row += [px] * cnt
            row = (row + [None] * w)[:w]
            rows.append(row)
            pos += nxt * 4
        return rows


def to_image(cel):
    from PIL import Image
    rows = cel.decode()
    im = Image.new('RGBA', (cel.width, cel.height))
    im.putdata([(0, 0, 0, 0) if px is None else px for r in rows for px in r])
    return im
