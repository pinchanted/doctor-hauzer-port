#!/usr/bin/env python3
"""Doctor Hauzer room file (OrgData/room/roomNNN.bin) parser.

Layout (all big-endian int32) as understood so far:

  0x00  tex_off      offset of texture bank (count + (offset,size) pairs, CEL chunks)
  0x04  sec_b_off    unknown section B (follows texture bank)
  0x08  sec_c_off    unknown section C
  0x0C  sec_c_size
  0x10  sec_e_off    unknown section E (to end of file)
  0x14  n_objects    number of object records that are active (approx.)
  0x18  id_base      object id base (100 -> ids 101, 102, ...)
  0x1C  mesh_off     room shell mesh offset
  0x20  ?            (0x9c8 in room001)
  0x3C  list_count   followed by a small list of ints (purpose TBD)
  0x80  object records, 0x80 bytes each, until mesh_off:
        +0x00..0x10  flags/state (TBD)
        +0x14        1
        +0x18        object id
        +0x1C        mesh offset
        +0x20        mesh size
        +0x24..0x2C  position x, y, z
        +0x10        state group (0 = initial room state; 1, 2... alternate states swapped in by events)
        +0x40        texture lookup table: face tex t -> bank index words[16 + t - 1]

Mesh:
  +0x00 n_verts, +0x04 n_faces, +0x08 ?, +0x0C..0x1C five -1 words (runtime pointers)
  +0x20 verts: n_verts * (x, y, z) int32 — y points DOWN (floor at y=+200)
  then faces: n_faces * (n, tex, v0, v1, v2, v3) int32; n is 4 for quads
  Quad corners map to CEL corners: v0=top-left, v1=top-right, v2=bottom-right, v3=bottom-left (verified visually).
"""
import struct
from dataclasses import dataclass, field


@dataclass
class Mesh:
    verts: list
    faces: list  # (n, tex, (v0, v1, v2, v3))
    unk: int = 0


@dataclass
class RoomObject:
    record_off: int
    words: tuple
    obj_id: int
    mesh: Mesh
    pos: tuple
    tex_override: int
    group: int = 0

    def bank_tex(self, t):
        i = 16 + t - 1
        return self.words[i] if 16 <= i < 32 else None


@dataclass
class Room:
    header: tuple
    shell: Mesh
    objects: list = field(default_factory=list)
    textures: list = field(default_factory=list)  # (offset, size) absolute
    data: bytes = b''


def read_mesh(d, off):
    nv, nf, unk = struct.unpack_from('>3I', d, off)
    p = off + 32
    verts = [struct.unpack_from('>3i', d, p + 12 * i) for i in range(nv)]
    p += 12 * nv
    faces = []
    for i in range(nf):
        n, tex, *vs = struct.unpack_from('>6i', d, p + 24 * i)
        faces.append((n, tex, tuple(vs[:n] if n <= 4 else vs)))
    return Mesh(verts, faces, unk)


def load(path):
    d = open(path, 'rb').read()
    hdr = struct.unpack_from('>16I', d, 0)
    tex_off, mesh_off = hdr[0], hdr[7]
    room = Room(hdr, read_mesh(d, mesh_off), data=d)
    for r in range(0x80, mesh_off, 0x80):
        w = struct.unpack_from('>32i', d, r)
        moff, msize = w[7], w[8]
        if not (0 < moff < len(d) and msize > 0):
            continue
        room.objects.append(RoomObject(r, w, w[6], read_mesh(d, moff), (w[9], w[10], w[11]), w[15], w[4]))
    n = struct.unpack_from('>I', d, tex_off)[0]
    for i in range(n):
        o, s = struct.unpack_from('>II', d, tex_off + 4 + 8 * i)
        room.textures.append((tex_off + o, s))
    return room


if __name__ == '__main__':
    import sys
    r = load(sys.argv[1])
    print('header', [hex(x) for x in r.header])
    print('shell: %d verts, %d faces' % (len(r.shell.verts), len(r.shell.faces)))
    print('textures:', len(r.textures))
    for o in r.objects:
        print(f'  obj {o.obj_id} @ {o.pos} verts={len(o.mesh.verts)} faces={len(o.mesh.faces)} tex_override={o.tex_override} words={o.words[:6]}')
