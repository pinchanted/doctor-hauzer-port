#!/usr/bin/env python3
"""Offline software render of a Doctor Hauzer room, for validating the format decoders.

usage: render_room.py roomNNN.bin out.png [--cam x y z --at x y z --w 960 --h 544 --fov 60]
"""
import argparse, math, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import room as roomfmt
from cel import Cel, to_image


def look_at(eye, at, up=(0, -1, 0)):
    eye, at, up = map(np.array, (eye, at, up))
    f = at - eye; f = f / np.linalg.norm(f)
    r = np.cross(f, up); r = r / np.linalg.norm(r)
    u = np.cross(r, f)
    return eye, np.stack([r, u, f])


def render(rm, eye, at, W, H, fov, tex_scale=1, skip_ids=(), group=0):
    texs = []
    for off, size in rm.textures:
        try:
            im = to_image(Cel(rm.data, off, off + size))
        except Exception:
            im = Image.new('RGBA', (2, 2), (255, 0, 255, 255))
        if tex_scale != 1:
            im = im.resize((im.width * tex_scale, im.height * tex_scale), Image.LANCZOS)
        texs.append(np.asarray(im, dtype=np.uint8))
    eye, R = look_at(eye, at)
    focal = (H / 2) / math.tan(math.radians(fov) / 2)
    color = np.zeros((H, W, 3), np.uint8); color[:] = (16, 16, 20)
    zbuf = np.full((H, W), np.inf)
    quads = []
    for n, t, vs in rm.shell.faces:
        if n == 4:
            quads.append(([rm.shell.verts[i] for i in vs], t, False))
    for o in rm.objects:
        if o.obj_id in skip_ids or o.group != group:
            continue
        for n, t, vs in o.mesh.faces:
            if n == 4:
                vv = [tuple(a + b for a, b in zip(o.mesh.verts[i], o.pos)) for i in vs]
                quads.append((vv, o.bank_tex(t), True))
    uv4 = [(0, 0), (1, 0), (1, 1), (0, 1)]
    ys, xs = np.mgrid[0:H, 0:W]
    for verts, t, is_obj in quads:
        if t is None or not (0 <= t < len(texs)):
            continue
        tex = texs[t]; th, tw = tex.shape[:2]
        cam = [(R @ (np.array(v) - eye)) for v in verts]
        for tri in ((0, 1, 2), (0, 2, 3)):
            P = [cam[i] for i in tri]
            if min(p[2] for p in P) < 5:
                continue  # crude near clip
            sx = [W / 2 + focal * p[0] / p[2] for p in P]
            sy = [H / 2 - focal * p[1] / p[2] for p in P]
            x0, x1 = max(int(min(sx)), 0), min(int(max(sx)) + 1, W)
            y0, y1 = max(int(min(sy)), 0), min(int(max(sy)) + 1, H)
            if x0 >= x1 or y0 >= y1:
                continue
            area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0])
            if abs(area) < 1e-6:
                continue
            px, py = xs[y0:y1, x0:x1] + 0.5, ys[y0:y1, x0:x1] + 0.5
            w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area
            w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area
            w2 = 1 - w0 - w1
            m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not m.any():
                continue
            iz = [1 / p[2] for p in P]
            invz = w0 * iz[0] + w1 * iz[1] + w2 * iz[2]
            z = 1 / invz
            uvs = [uv4[i] for i in tri]
            u = (w0 * uvs[0][0] * iz[0] + w1 * uvs[1][0] * iz[1] + w2 * uvs[2][0] * iz[2]) * z
            v = (w0 * uvs[0][1] * iz[0] + w1 * uvs[1][1] * iz[1] + w2 * uvs[2][1] * iz[2]) * z
            tu = np.clip((u * tw).astype(int), 0, tw - 1)
            tv = np.clip((v * th).astype(int), 0, th - 1)
            texel = tex[tv, tu]
            zb = zbuf[y0:y1, x0:x1]
            # objects (doors, windows) sit on the wall plane: let them win ties, like the 3DO's draw order
            m &= ((z <= zb + 2.0) if is_obj else (z < zb)) & (texel[..., 3] > 0)
            zb[m] = z[m]
            color[y0:y1, x0:x1][m] = texel[..., :3][m]
    return Image.fromarray(color)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('room'); ap.add_argument('out')
    ap.add_argument('--cam', nargs=3, type=float, default=(350, -150, 550))
    ap.add_argument('--at', nargs=3, type=float, default=(-150, 100, -300))
    ap.add_argument('--w', type=int, default=960); ap.add_argument('--h', type=int, default=544)
    ap.add_argument('--fov', type=float, default=60)
    ap.add_argument('--skip', nargs='*', type=int, default=[])
    ap.add_argument('--tex-scale', type=int, default=1)
    ap.add_argument('--group', type=int, default=0)
    a = ap.parse_args()
    render(roomfmt.load(a.room), a.cam, a.at, a.w, a.h, a.fov, a.tex_scale, set(a.skip), a.group).save(a.out)
