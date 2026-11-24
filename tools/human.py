#!/usr/bin/env python3
"""Player model: OrgData/human/HumanPol.bin (69 meshes) + HumanTex.bin (texture bank).

HumanPol.bin: u32 count, count x (u32 offset, u32 size), then meshes in the room mesh format
(variable-length faces). 0-46 body poses (vertex animation: 91 verts, 96 faces), 47-56 close-up
heads, 57 in-game head, 58-59 hands, 60-63 YES/NO choice panels, 64-68 held items. Faces map
through per-mesh texture tables stored in launchme (TABLE_FOR_MESH).

usage: human.py <human dir> <launchme> <out.png>   (contact sheet of every mesh)
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
import room as roomfmt


# texture table per mesh, from launchme's loader at 0x3480 (tables live in the exe's data at
# 0x322fc..0x32bfc, 16 (type, value) pairs each: type 2 = HumanTex, 3 = ItemTex, 4 = TBD, 0 = none)
TABLE_FOR_MESH = {**{i: 0x322fc for i in range(47)}, 47: 0x329fc, 48: 0x329fc, 49: 0x32a7c, 50: 0x32afc,
                  **{i: 0x329fc for i in range(51, 57)}, 57: 0x326fc, 58: 0x32b7c, 59: 0x32b7c, 60: 0x3217c,
                  61: 0x3237c, 62: 0x323fc, 63: 0x3247c, 64: 0x327fc, 65: 0x3277c, 66: 0x3287c, 67: 0x328fc,
                  68: 0x3297c}


def read_tables(launchme_path):
    d = open(launchme_path, 'rb').read()
    out = {}
    for a in set(TABLE_FOR_MESH.values()):
        w = struct.unpack_from('>32i', d, a)
        out[a] = list(zip(w[0::2], w[1::2]))
    return out


# attachment markers in every body pose (0-46): thin triangles textured with table entry 14,
# (attach point, forward, up) - the engine places a rigid part on each one per frame
MARKER_VERTS = {'hand_l': (62, 63, 64), 'hand_r': (65, 66, 67), 'head': (68, 69, 70)}
PART_MESH = {'head': 57, 'hand_l': 58, 'hand_r': 59}


def _basis(p0, p1, p2):
    import numpy as np
    o, a, b = (np.array(p, float) for p in (p0, p1, p2))
    f = a - o; f /= np.linalg.norm(f)
    u = b - o; u -= f * (u @ f); u /= np.linalg.norm(u)
    r = np.cross(u, f)
    # mesh-local axes: +x -> r, +y -> -u (y points down), +z -> -f (parts face -z)
    return o, np.stack([r, -u, -f], axis=1)


class Model:
    def __init__(self, human_dir, launchme=None):
        d = open(os.path.join(human_dir, 'HumanPol.bin'), 'rb').read()
        n = struct.unpack_from('>I', d, 0)[0]
        self.entries = [struct.unpack_from('>II', d, 4 + 8 * i) for i in range(n)]
        self.meshes = [roomfmt.read_mesh(d, o) for o, s in self.entries]
        # one combined texture list: HumanTex entries, then ItemTex entries
        t = open(os.path.join(human_dir, 'HumanTex.bin'), 'rb').read()
        it = open(os.path.join(os.path.dirname(os.path.abspath(human_dir)), 'item', 'ItemTex.bin'), 'rb').read()
        k = struct.unpack_from('>I', t, 0)[0]
        ki = struct.unpack_from('>I', it, 0)[0]
        self.tex_data = t + it
        self.textures = [(o, s) for o, s in (struct.unpack_from('>II', t, 4 + 8 * i) for i in range(k))]
        self.n_human = k
        self.textures += [(len(t) + o, s) for o, s in (struct.unpack_from('>II', it, 4 + 8 * i) for i in range(ki))]
        self.tables = read_tables(launchme) if launchme else {}

    def table(self, i):
        """Face tex -> index into self.textures, via the exe's per-mesh table."""
        pairs = self.tables.get(TABLE_FOR_MESH.get(i))
        if not pairs:
            return list(range(64))
        out = []
        for typ, val in pairs:
            out.append(val if typ == 2 else (self.n_human + val) if typ == 3 else None)
        return out

    def obj(self, i, pos=(0, 0, 0), rot=(0, 0, 0)):
        tbl = tuple(self.table(i))
        o = roomfmt.RoomObject(0, (), i, self.meshes[i], pos, rot, tbl, 0, True, 0, 0)
        o.bank_tex = lambda t, tbl=tbl: tbl[t] if 0 <= t < len(tbl) else None
        return o

    def assembled(self, frame, pos=(0, 0, 0), rot=(0, 0, 0), head=57):
        """Body pose `frame` with head and hands attached at its markers, as RoomObjects.
        Marker triangles are dropped from the body."""
        import numpy as np
        body = self.meshes[frame]
        marker_idx = {v for vs in MARKER_VERTS.values() for v in vs}
        faces = [f for f in body.faces if not set(f[2]) <= marker_idx]
        objs = [self.obj(frame, pos, rot)]
        objs[0].mesh = roomfmt.Mesh(body.verts, faces)
        for part, vs in MARKER_VERTS.items():
            mi = head if part == 'head' else PART_MESH[part]
            o, R = _basis(*(body.verts[v] for v in vs))
            local = np.array(self.meshes[mi].verts, float)
            world = (local @ R.T) + o
            ob = self.obj(mi, pos, rot)
            ob.mesh = roomfmt.Mesh([tuple(p) for p in world], self.meshes[mi].faces)
            objs.append(ob)
        return objs

    def into_room(self, rm, frame, pos, rot=(0, 0, 0)):
        """A copy of Room `rm` with the assembled player added (textures appended to its bank).
        pos is the feet position in room space (floor y = +200); rot y in 256ths of a turn."""
        base = len(rm.textures)
        objs = []
        for ob in self.assembled(frame, pos, rot):
            tbl = tuple(None if t is None else t + base for t in ob.table)
            ob.table = tbl
            ob.bank_tex = lambda t, tbl=tbl: tbl[t] if 0 <= t < len(tbl) else None
            ob.area = 0
            objs.append(ob)
        textures = list(rm.textures) + [(len(rm.data) + o, s) for o, s in self.textures]
        return roomfmt.Room(rm.header, list(rm.objects) + objs, textures, rm.cameras, rm.zones, rm.triggers,
                            rm.data + self.tex_data)

    def as_room(self, *meshes, pos=(0, 0, 0), rot=(0, 0, 0)):
        """Wrap one or more meshes as a Room so render_room can draw them."""
        return roomfmt.Room((), [self.obj(i, pos, rot) for i in meshes], self.textures, data=self.tex_data)


if __name__ == '__main__':
    import render_room
    from PIL import Image, ImageDraw
    mdl = Model(sys.argv[1], sys.argv[2])
    ims = []
    for i, m in enumerate(mdl.meshes):
        xs = [v[0] for v in m.verts] or [0]; ys = [v[1] for v in m.verts] or [0]
        cy = (min(ys) + max(ys)) / 2
        h = max(max(ys) - min(ys), max(xs) - min(xs), 60)
        im = render_room.render(mdl.as_room(i), (h * 0.5, cy - h * 0.1, h * 1.9), (0, cy, 0), 200, 240, 45,
                                bilinear=True)
        ImageDraw.Draw(im).text((4, 4), f'{i}  v{len(m.verts)} f{len(m.faces)}', fill=(255, 255, 0))
        ims.append(im)
    cols = 12
    S = Image.new('RGB', (cols * 202, ((len(ims) + cols - 1) // cols) * 242))
    for i, im in enumerate(ims):
        S.paste(im, ((i % cols) * 202, (i // cols) * 242))
    S.save(sys.argv[3])
