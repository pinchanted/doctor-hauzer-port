#!/usr/bin/env python3
"""Offline software render of a Doctor Hauzer room, for validating formats and judging HD textures.

usage: render_room.py roomNNN.bin out.png [--camera N | --cam x y z --at x y z] [--w 960 --h 544 --fov 60]
                      [--group 0] [--skip id ...] [--hd-dir DIR] [--filter nearest|bilinear] [--ssaa 2]

--hd-dir: folder(s) of replacement textures named <hash>.png (hash = sha1 of the CEL bytes, first
16 hex digits, as written by export_textures.py). Any size; UVs are normalised.
"""
import argparse, hashlib, math, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import room as roomfmt
from cel import Cel, to_image


# The game's projection (launchme 0x102bc called with (-64, 192); used by the draw routines 0x13c50 /
# 0x14118): screen = v * 256 / (z + 64) + centre on a 320x240 screen, z = camera-space depth from the
# camera position. So the focal length is 256 px and the true eye sits 64 units behind the camera
# position. Vertical FOV = 2*atan(120/256) = 50.19 deg (horizontal 64.0 deg at 4:3).
# Within 256 units of the eye the game swaps 1/d for a linear approximation (not reproduced).
GAME_FOV = math.degrees(2 * math.atan(120 / 256))
GAME_EYE_BACK = 64


def game_camera(eye, at):
    """Camera table (eye, target) -> the eye the game actually projects from."""
    e, a = np.array(eye, float), np.array(at, float)
    f = (a - e) / np.linalg.norm(a - e)
    return tuple(e - f * GAME_EYE_BACK), tuple(at)


def look_at(eye, at, up=(0, -1, 0)):
    eye, at, up = map(lambda v: np.array(v, float), (eye, at, up))
    f = at - eye; f = f / np.linalg.norm(f)
    r = np.cross(f, up)
    if np.linalg.norm(r) < 1e-6:        # looking straight up/down: pick another up vector
        r = np.cross(f, np.array((0, 0, 1.0)))
    r = r / np.linalg.norm(r)
    u = np.cross(r, f)
    return eye, np.stack([r, u, f])


def tex_hash(raw):
    return hashlib.sha1(raw).hexdigest()[:16]


def load_textures(rm, hd_dir=None):
    texs = []
    for off, size in rm.textures:
        raw = rm.data[off:off + size]
        im = None
        for d in ([hd_dir] if isinstance(hd_dir, str) else (hd_dir or [])):
            p = os.path.join(d, tex_hash(raw) + '.png')
            if os.path.exists(p):
                im = Image.open(p).convert('RGBA')
                break
        if im is None:
            try:
                im = to_image(Cel(raw))
            except Exception:
                im = Image.new('RGBA', (2, 2), (255, 0, 255, 255))
        a = np.asarray(im, dtype=np.float32).copy()
        a[..., :3] *= a[..., 3:4] / 255.0  # premultiply so bilinear filtering doesn't bleed black edges
        texs.append(a)
    return texs


def sample(tex, u, v, bilinear):
    th, tw = tex.shape[:2]
    if not bilinear:
        tu = np.clip((u * tw).astype(int), 0, tw - 1)
        tv = np.clip((v * th).astype(int), 0, th - 1)
        return tex[tv, tu]
    x = u * tw - 0.5; y = v * th - 0.5
    x0 = np.floor(x); y0 = np.floor(y)
    fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
    x0 = x0.astype(int); y0 = y0.astype(int)
    x1 = np.clip(x0 + 1, 0, tw - 1); y1 = np.clip(y0 + 1, 0, th - 1)
    x0 = np.clip(x0, 0, tw - 1); y0 = np.clip(y0, 0, th - 1)
    top = tex[y0, x0] * (1 - fx) + tex[y0, x1] * fx
    bot = tex[y1, x0] * (1 - fx) + tex[y1, x1] * fx
    return top * (1 - fy) + bot * fy


def _collapsed_quad(p, t, is_obj, n=4):
    p0, p1, p2 = (np.array(v, float) for v in p)
    P = lambda u, v: (1 - v) * ((1 - u) * p0 + u * p1) + v * p2
    out = []
    for i in range(n):
        for j in range(n):
            u0, u1, v0, v1 = i / n, (i + 1) / n, j / n, (j + 1) / n
            out.append(([tuple(P(u0, v0)), tuple(P(u1, v0)), tuple(P(u1, v1)), tuple(P(u0, v1))], t, is_obj,
                        [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]))
    return out


def render(rm, eye, at, W, H, fov, tex_scale=1, skip_ids=(), group=0, hd_dir=None,
           bilinear=False, ssaa=1):
    W0, H0 = W, H
    W, H = W * ssaa, H * ssaa
    texs = load_textures(rm, hd_dir)
    eye, R = look_at(eye, at)
    focal = (H / 2) / math.tan(math.radians(fov) / 2)
    color = np.zeros((H, W, 3), np.float32); color[:] = (16, 16, 20)
    zbuf = np.full((H, W), np.inf)
    quads = []
    for o in rm.objects:
        if o.obj_id in skip_ids or o.area != group or not o.visible:
            continue
        wv = o.world_verts()
        is_obj = o is not rm.objects[0]
        for n, t, vs in o.mesh.faces:
            if n == 4:
                quads.append(([wv[i] for i in vs], o.bank_tex(t), is_obj))
            elif n == 3:
                # the 3DO draws a triangle as a CEL quad with its last corner collapsed onto v2, so
                # the whole texture is squeezed into the triangle: approximate with bilinear patches
                quads.extend(_collapsed_quad([wv[i] for i in vs], o.bank_tex(t), is_obj))
    uv4 = [(0, 0), (1, 0), (1, 1), (0, 1)]
    for q in quads:
        verts, t, is_obj = q[:3]
        quv = q[3] if len(q) > 3 else uv4
        if t is None or not (0 <= t < len(texs)):
            continue
        tex = texs[t]
        cam = [(R @ (np.array(v, float) - eye)) for v in verts]
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
            py, px = np.mgrid[y0:y1, x0:x1].astype(np.float32) + 0.5
            w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area
            w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area
            w2 = 1 - w0 - w1
            m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not m.any():
                continue
            iz = [1 / p[2] for p in P]
            invz = w0 * iz[0] + w1 * iz[1] + w2 * iz[2]
            z = 1 / invz
            uvs = [quv[i] for i in tri]
            u = (w0 * uvs[0][0] * iz[0] + w1 * uvs[1][0] * iz[1] + w2 * uvs[2][0] * iz[2]) * z
            v = (w0 * uvs[0][1] * iz[0] + w1 * uvs[1][1] * iz[1] + w2 * uvs[2][1] * iz[2]) * z
            texel = sample(tex, u, v, bilinear)
            alpha = texel[..., 3]
            zb = zbuf[y0:y1, x0:x1]
            # objects (doors, windows) sit on the wall plane: let them win ties, like the 3DO's draw order
            m &= ((z <= zb + 2.0) if is_obj else (z < zb)) & (alpha >= 127.5)
            zb[m] = z[m]
            rgb = texel[..., :3] * (255.0 / np.maximum(alpha, 1.0))[..., None]  # un-premultiply
            color[y0:y1, x0:x1][m] = rgb[m]
    img = Image.fromarray(np.clip(color + 0.5, 0, 255).astype(np.uint8))
    if ssaa > 1:
        img = img.resize((W0, H0), Image.BOX)
    return img


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('room'); ap.add_argument('out')
    ap.add_argument('--cam', nargs=3, type=float, default=(350, -150, 550))
    ap.add_argument('--at', nargs=3, type=float, default=(-150, 100, -300))
    ap.add_argument('--w', type=int, default=960); ap.add_argument('--h', type=int, default=544)
    ap.add_argument('--fov', type=float, default=None, help='vertical; default: the game projection')
    ap.add_argument('--skip', nargs='*', type=int, default=[])
    ap.add_argument('--group', '--area', type=int, default=0, help='room area (0 = the room itself)')
    ap.add_argument('--camera', type=int, default=None, help="use the room's own camera N")
    ap.add_argument('--hd-dir', nargs='*', default=None)
    ap.add_argument('--filter', choices=('nearest', 'bilinear'), default='nearest')
    ap.add_argument('--ssaa', type=int, default=1)
    a = ap.parse_args()
    rm = roomfmt.load(a.room)
    if a.camera is not None:
        a.cam, a.at = game_camera(rm.cameras[a.camera].eye, rm.cameras[a.camera].target)
    if a.fov is None:
        a.fov = GAME_FOV
    render(rm, a.cam, a.at, a.w, a.h, a.fov, 1, set(a.skip), a.group,
           a.hd_dir, a.filter == 'bilinear', a.ssaa).save(a.out)
