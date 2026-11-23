#!/usr/bin/env python3
"""Extract files from a 3DO Opera filesystem image (2048-byte sectors)."""
import os, struct, sys, json

def be32(b, o): return struct.unpack_from('>I', b, o)[0]
def sbe32(b, o): return struct.unpack_from('>i', b, o)[0]

class Opera:
    def __init__(self, path):
        self.f = open(path, 'rb')
        hdr = self.read_blocks(0, 1, 2048)
        assert hdr[0] == 1 and hdr[1:6] == b'\x5a' * 5, 'not an Opera volume'
        self.label = hdr[0x28:0x48].rstrip(b'\0').decode('ascii', 'replace')
        self.block_size = be32(hdr, 0x4C)
        self.block_count = be32(hdr, 0x50)
        self.root_blocks = be32(hdr, 0x58)
        self.root_bsize = be32(hdr, 0x5C)
        last = be32(hdr, 0x60)
        self.root_avatars = [be32(hdr, 0x64 + 4 * i) for i in range(last + 1)]

    def read_blocks(self, blk, n, bsize=2048):
        self.f.seek(blk * 2048)
        return self.f.read(n * bsize)

    def list_dir(self, avatar, nblocks, bsize):
        entries, idx = [], 0
        while idx >= 0 and idx < max(nblocks, 1) + 64:
            blk = self.read_blocks(avatar + idx * bsize // 2048, 1, bsize)
            nxt = sbe32(blk, 0); first_free = be32(blk, 0x0C); off = be32(blk, 0x10)
            while off < first_free:
                flags = be32(blk, off)
                typ = blk[off + 8:off + 12].decode('latin-1')
                e = dict(flags=flags, id=be32(blk, off + 4), type=typ,
                         block_size=be32(blk, off + 12), size=be32(blk, off + 16),
                         blocks=be32(blk, off + 20), burst=be32(blk, off + 24), gap=be32(blk, off + 28),
                         name=blk[off + 32:off + 64].split(b'\0')[0].decode('latin-1'))
                last = be32(blk, off + 64)
                e['avatars'] = [be32(blk, off + 68 + 4 * i) for i in range(last + 1)]
                e['is_dir'] = (flags & 0xFF) == 7
                entries.append(e)
                off += 68 + 4 * (last + 1)
                if flags & 0xC0000000: break
            if nxt == -1 or nxt == 0xFFFFFFFF: break
            idx = nxt
        return entries

    def walk(self, avatar, nblocks, bsize, path, out, manifest):
        for e in self.list_dir(avatar, nblocks, bsize):
            p = os.path.join(path, e['name'])
            rel = os.path.relpath(p, out)
            manifest.append({k: e[k] for k in ('name', 'type', 'size', 'blocks', 'block_size', 'avatars', 'is_dir')} | {'path': rel})
            if e['is_dir']:
                os.makedirs(p, exist_ok=True)
                self.walk(e['avatars'][0], e['blocks'], e['block_size'], p, out, manifest)
            else:
                self.f.seek(e['avatars'][0] * 2048)
                with open(p, 'wb') as o:
                    remaining = e['size']
                    while remaining:
                        chunk = self.f.read(min(remaining, 1 << 20))
                        if not chunk: break
                        o.write(chunk); remaining -= len(chunk)

if __name__ == '__main__':
    img, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    v = Opera(img)
    print('label', v.label, 'blocks', v.block_count, 'root avatars', v.root_avatars)
    manifest = []
    v.walk(v.root_avatars[0], v.root_blocks, v.root_bsize, out, out, manifest)
    with open(os.path.join(out, '..', 'manifest.json'), 'w') as m:
        json.dump(manifest, m, indent=1)
    print('entries', len(manifest))
