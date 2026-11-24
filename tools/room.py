#!/usr/bin/env python3
"""Doctor Hauzer room file (OrgData/room/roomNNN.bin) parser.

Verified against launchme's loader (0x148dc), object builder (0x149dc), collision builder
(0x12ae0), renderer (0x12ee4) and collision test (0x10ee0). All values big-endian int32.

Header
  0x00  texture bank offset (count, then (offset, size) pairs relative to the bank; CEL chunks)
  0x04  unused by the loader (stale build-tool value; points into the texture bank)
  0x08  collision/camera grid offset ("Atari Table"), 0x0C its size (always 0xC80)
  0x10  camera table offset
  0x14  record count hint, 0x1C shell mesh offset, 0x20 shell mesh size (same as record 0)

Object records: 0x80 bytes each from 0x18 until index == -1. Record 0 is the room shell.
  +0x00 object index (100, 101, ...)      +0x04 mesh offset   +0x08 mesh size
  +0x0C position x, y, z                  +0x18 rotation x, y, z (256 units per turn)
  +0x24 texture table, 16 entries: face tex t -> bank index table[t]
  +0x64, +0x68, +0x6C  flags (TBD)
  +0x70 collidable/selectable flag        +0x74 trigger id (matches script ON (id, ...) events)
  +0x78 area: objects exist only while the global current area equals this. Area 0 is the
        room itself; areas 1+ are extra scenes in the same file (e.g. door-opening views)
  +0x7C initially visible

Mesh: n_verts, n_faces, runtime size (6*n_verts+16), five -1 words; then n_verts*(x,y,z), y DOWN
  (floor at +200); then n_faces variable-length faces (n, tex, v0..v[n-1]): triangles and quads.
  Quad corners map to CEL corners
  v0=top-left, v1=top-right, v2=bottom-right, v3=bottom-left.

Collision/camera grid: 20x20 cells of 200 units covering x,z in [-2000, 2000], cell (i, j) spans
  x = -2000+200*i .. +200, z = 2000-200*j .. -200. Layer 1 (400 ints): camera zone (camera id
  used while the player stands there) or 99 = solid. Layer 2 (400 ints at +0x640): trigger id
  (script event) or 99 = solid.

Camera table: 32-byte entries (id, eye x, y, z, target x, y, z, 0) with id == entry index;
  ends at the first mismatch. Camera 0 is an overhead view.
"""
import math, struct
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
    rot: tuple
    table: tuple
    area: int
    visible: bool
    trigger: int
    collidable: int

    @property
    def group(self):  # backwards-compatible name
        return self.area

    def bank_tex(self, t):
        return self.table[t] if 0 <= t < 16 else None

    def world_verts(self):
        rx, ry, rz = (a * 2 * math.pi / 256 for a in self.rot)
        out = []
        for x, y, z in self.mesh.verts:
            if rx:
                y, z = y * math.cos(rx) - z * math.sin(rx), y * math.sin(rx) + z * math.cos(rx)
            if ry:
                x, z = x * math.cos(ry) + z * math.sin(ry), -x * math.sin(ry) + z * math.cos(ry)
            if rz:
                x, y = x * math.cos(rz) - y * math.sin(rz), x * math.sin(rz) + y * math.cos(rz)
            out.append((x + self.pos[0], y + self.pos[1], z + self.pos[2]))
        return out


@dataclass
class Camera:
    cam_id: int
    eye: tuple
    target: tuple


@dataclass
class Room:
    header: tuple
    objects: list = field(default_factory=list)
    textures: list = field(default_factory=list)  # (offset, size) absolute
    cameras: list = field(default_factory=list)
    zones: list = field(default_factory=list)     # 20x20 camera zone / 99 solid
    triggers: list = field(default_factory=list)  # 20x20 trigger id / 99 solid
    data: bytes = b''

    @property
    def shell(self):
        return self.objects[0].mesh if self.objects else Mesh([], [])

    def areas(self):
        return sorted({o.area for o in self.objects})


def read_mesh(d, off):
    """Faces are variable length: (n, tex, v0 .. v[n-1]) - triangles 5 words, quads 6."""
    nv, nf, unk = struct.unpack_from('>3I', d, off)
    p = off + 32
    verts = [struct.unpack_from('>3i', d, p + 12 * i) for i in range(nv)]
    p += 12 * nv
    faces = []
    for _ in range(nf):
        if p + 8 > len(d):
            break
        n, tex = struct.unpack_from('>2i', d, p)
        if not 1 <= n <= 8 or p + 8 + 4 * n > len(d):  # a few 2-vertex (line) faces exist
            break
        vs = struct.unpack_from(f'>{n}i', d, p + 8)
        if all(0 <= v < nv for v in vs):
            faces.append((n, tex, vs))
        p += 8 + 4 * n
    m = Mesh(verts, faces, unk)
    m.end = p
    return m


def load(path):
    d = open(path, 'rb').read()
    hdr = struct.unpack_from('>16I', d, 0)
    room = Room(hdr, data=d)
    k = 0
    while 0x18 + 0x80 * (k + 1) <= len(d):
        off = 0x18 + 0x80 * k
        w = struct.unpack_from('>32i', d, off)
        if w[0] == -1:
            break
        if 0 < w[1] and w[2] >= 32 and w[1] + w[2] <= len(d):
            room.objects.append(RoomObject(off, w, w[0], read_mesh(d, w[1]), tuple(w[3:6]), tuple(w[6:9]),
                                           tuple(w[9:25]), w[30], bool(w[31]), w[29], w[28]))
        k += 1
    tex_off = hdr[0]
    n = struct.unpack_from('>I', d, tex_off)[0]
    for i in range(n):
        o, s = struct.unpack_from('>II', d, tex_off + 4 + 8 * i)
        room.textures.append((tex_off + o, s))
    C = hdr[2]
    room.zones = list(struct.unpack_from('>400i', d, C))
    room.triggers = list(struct.unpack_from('>400i', d, C + 0x640))
    E = hdr[4]
    for i in range(64):
        if E + 32 * (i + 1) > len(d):
            break
        w = struct.unpack_from('>8i', d, E + 32 * i)
        if w[0] != i:
            break
        room.cameras.append(Camera(i, tuple(w[1:4]), tuple(w[4:7])))
    return room


if __name__ == '__main__':
    import sys
    r = load(sys.argv[1])
    print(f'{len(r.objects)} objects, areas {r.areas()}, {len(r.textures)} textures, {len(r.cameras)} cameras')
    for o in r.objects:
        print(f'  obj {o.obj_id} area {o.area} {"visible" if o.visible else "hidden "} pos {o.pos} rot {o.rot} '
              f'trigger {o.trigger} faces {len(o.mesh.faces)}')
    for c in r.cameras:
        print(f'  camera {c.cam_id}: eye {c.eye} -> {c.target}')
    for j in range(20):
        row = r.zones[j * 20:j * 20 + 20]
        if any(row):
            print('  z=%5d ' % (2000 - 200 * j) + ' '.join('##' if v == 99 else (f'{v:2d}' if v else ' .') for v in row))
