#!/usr/bin/env python3
"""Doctor Hauzer upscaling test: one command does everything.

1. Downloads the candidate models (checked against SHA-256) into C:\\dev\\ai\\upscale_models.
2. Prepares every test image: wrap padding for textures (no seams), colour bleed for
   transparent textures (no dark fringes), reflect padding for stills.
3. Runs every model with three pre-passes (none / deband / dedither) -> 27 variants, plus
   baselines (original, Lanczos).
4. Renders six rooms at 1920x1080 with each variant's textures (bilinear, 2x supersampled).
5. Writes results/compare.html: A/B slider and grid viewer, with a picks tally.

Re-running skips finished work. Options: --only <model keys>, --no-renders, --fp16, --quick (smoke test).
Runs with ComfyUI portable's Python (PyTorch + CUDA + spandrel); see run_upscale_test.bat.
"""
import argparse, concurrent.futures as cf, hashlib, json, os, shutil, sys, time, traceback, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)  # embedded Python does not add the script folder to sys.path

import numpy as np
from PIL import Image

PROJECT = os.path.dirname(TOOLS)
TEST = os.path.join(PROJECT, 'upscale_test')
ORIG = os.path.join(TEST, 'originals')
RESULTS = os.path.join(TEST, 'results')
MODEL_DIR = r'C:\dev\ai\upscale_models' if os.name == 'nt' else os.path.join(TEST, 'models')
ORGDATA = os.path.join(PROJECT, 'extracted', 'fs', 'OrgData')
TEXDIR = os.path.join(PROJECT, 'extracted', 'textures')

SCALE = 4
PAD = 16  # source pixels of context around each image; cropped off after upscaling

GH = 'https://github.com/'
GD = 'https://drive.usercontent.google.com/download?export=download&confirm=t&id='
# key, display name, file, sha256, urls, what it's for
MODELS = [
    ('pbrify_rplksr', '4x-PBRify_RPLKSRd_V3', '4x-PBRify_RPLKSRd_V3.pth',
     '1325a08842ebfc18aea90e71594aa9b9e82b9c7e16321dccf5b757884a22daf1',
     [GH + 'Kim2091/Kim2091-Models/releases/download/4x-PBRify_RPLKSRd_V3/4x-PBRify_RPLKSRd_V3.pth'],
     'game textures; built-in dedither/deband'),
    ('pbrify_dat2', '4x-PBRify_UpscalerDAT2_V1', '4x-PBRify_UpscalerDAT2_V1.pth',
     'ff133c35707986e253a9e84aae16ef0295847de41239f1eda53fbbeb0e2243b7',
     [GH + 'Kim2091/Kim2091-Models/releases/download/4x-PBRify_UpscalerDAT2_V1/4x-PBRify_UpscalerDAT2_V1.pth'],
     'game textures (DAT2)'),
    ('gtav_rgt', '4xTextures_GTAV_rgt-s', '4xTextures_GTAV_rgt-s.safetensors',
     '11ae0fdafe0c7cef12b0be71761fd03c6319c787ca678a9120df59e2d66e5607',
     [GH + 'Phhofm/models/releases/download/4xTextures_GTAV_rgt-s/4xTextures_GTAV_rgt-s.safetensors'],
     'game textures (GTA V)'),
    ('gtav_rgt_dither', '4xTextures_GTAV_rgt-s_dither', '4xTextures_GTAV_rgt-s_dither.safetensors',
     'ef850c4fd1d1ab81eb8cf63a0e11c62adfead07ec3a34f81675e986e76800f3f',
     [GH + 'Phhofm/models/releases/download/4xTextures_GTAV_rgt-s_dither/4xTextures_GTAV_rgt-s_dither.safetensors'],
     'game textures, trained to remove dithering'),
    ('texture_dat2', '4xTextureDAT2_otf', '4xTextureDAT2_otf.safetensors',
     '5851f91a4c95d803c919dd279a9353e32b212495cf54c5fbc868eed23b9f068b',
     [GH + 'Phhofm/models/releases/download/4xTextureDAT2_otf/4xTextureDAT2_otf.safetensors'],
     'game textures (DAT2)'),
    ('ultrasharp_v2', '4x-UltraSharpV2', '4x-UltraSharpV2.safetensors',
     '2b5db674aa8f3864a696dccd1d07d7cc0500fbf220f35044cd30eee9a7f971a7',
     ['https://huggingface.co/Kim2091/UltraSharpV2/resolve/main/4x-UltraSharpV2.safetensors?download=true'],
     'sharp general purpose (DAT2)'),
    ('lollypop', '4x-Lollypop', '4x-Lollypop.pth',
     '4e609039d8ece82dd6aba14b692181c6722a461a762eef538700683dfef6fb6e',
     [GD + '10h8YXKKOQ61ANnwLjjHqXJdn4SbBuUku'],
     'pre-rendered images, dedithering'),
    ('nomos2_dat2', '4xNomos2_hq_dat2', '4xNomos2_hq_dat2.pth',
     '1f2be2b4786b5031776aed90e1818564f5576ab1e44da9355c7f3788ea25bdea',
     [GH + 'Phhofm/models/releases/download/4xNomos2_hq_dat2/4xNomos2_hq_dat2.pth'],
     'realistic/photo (DAT2)'),
    ('nomos8k_hatl', '4xNomos8kSCHAT-L', '4xNomos8kSCHAT-L.pth',
     '9e7726ba191fdf05b87ea8585d78164b6e96e2ee04fdbb3285c3efa37db4b5b0',
     [GD + '1gh7HDKzf9aZw-rA8WYQy1ZZ8D0MAIHxR'],
     'realistic/photo (HAT-L, slow)'),
]
PREPASSES = [
    ('deband', '1x-Debandurh-FS-Ultra-lite', '1x-Debandurh-FS-Ultra-lite.pth',
     '027dfbf10e0ff3f8347960106f89443241b85e91edbd7329d3ded50829ee98f7',
     ['https://objectstorage.us-phoenix-1.oraclecloud.com/n/ax6ygfvpvzka/b/open-modeldb-files/o/1x-Debandurh-FS-Ultra-lite.pth',
      'https://f002.backblazeb2.com/file/ESRGAN/_RELEASE/1x_Debandurh_FS_lite_140000_G.pth'],
     'removes colour banding'),
    ('dedither', '1x-DitherDeleterV3-Smooth', '1x-DitherDeleterV3-Smooth.pth',
     '1e98cb5427d16baeb83033c574d1ca4d8dadf05dd043b32c3ba98f75e30f6205',
     [GD + '1C6RTSFkBnidfBxBI6HjasgUEIgAzG3l3'],
     'removes dithering'),
]

# Rooms rendered for in-context comparison: (id, room, camera, look-at, hidden object ids). None = automatic corner camera.
RENDERS = [
    ('room001_hall', 'room001', None, None, []),
    ('room007_lounge', 'room007', None, None, []),
    ('room012_library', 'room012', None, None, []),
    ('room208_ghost', 'room208', None, None, []),
    ('room301_cellar', 'room301', None, None, []),
    ('room303_frieze', 'room303', None, None, []),
]
RENDER_W, RENDER_H, RENDER_FOV, RENDER_SSAA = 1920, 1080, 50.19, 2  # game projection: 2*atan(120/256)

LOG = None


def log(*a):
    s = ' '.join(str(x) for x in a)
    print(s, flush=True)
    if LOG:
        LOG.write(time.strftime('%H:%M:%S ') + s + '\n'); LOG.flush()


# ---------------------------------------------------------------- downloads
def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def fetch(entry):
    key, name, fn, sha, urls, _ = entry
    os.makedirs(MODEL_DIR, exist_ok=True)
    path = os.path.join(MODEL_DIR, fn)
    if os.path.exists(path) and sha256(path) == sha:
        return path
    for url in urls:
        tmp = path + '.part'
        try:
            log(f'  downloading {name} ...')
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req, timeout=120) as r, open(tmp, 'wb') as f:
                shutil.copyfileobj(r, f, 1 << 20)
            got = sha256(tmp)
            if got == sha:
                os.replace(tmp, path)
                log(f'  ok   {name} ({os.path.getsize(path) / 1e6:.0f} MB, sha256 verified)')
                return path
            log(f'  WARNING {name}: checksum mismatch from {url.split("/")[2]} (got {got[:12]}...)')
        except Exception as e:
            log(f'  WARNING {name}: download failed from {url.split("/")[2]}: {e}')
        if os.path.exists(tmp):
            os.remove(tmp)
    log(f'  SKIP {name}: could not download it; the test continues without it')
    return None


# ---------------------------------------------------------------- images
def bleed(rgb, alpha, iters=64):
    """Spread opaque colours into transparent pixels so the model never sees black edges."""
    rgb = rgb.copy()
    known = alpha > 0
    if known.all() or not known.any():
        return rgb
    for _ in range(iters):
        if known.all():
            break
        acc = np.zeros_like(rgb); cnt = np.zeros(known.shape, np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
            k = np.roll(np.roll(known, dy, 0), dx, 1)
            c = np.roll(np.roll(rgb, dy, 0), dx, 1)
            acc += c * k[..., None]; cnt += k
        new = (~known) & (cnt > 0)
        rgb[new] = acc[new] / cnt[new][:, None]
        known = known | new
    return rgb


class Item:
    """One image to upscale."""

    def __init__(self, set_name, key, path):
        self.set, self.key, self.path = set_name, key, path
        im = Image.open(path)
        self.has_alpha = im.mode in ('RGBA', 'LA', 'P') and np.asarray(im.convert('RGBA'))[..., 3].min() < 255
        a = np.asarray(im.convert('RGBA'), dtype=np.float32) / 255.0
        self.alpha = a[..., 3]
        rgb = a[..., :3]
        self.h, self.w = rgb.shape[:2]
        self.tiny = self.w < 8 or self.h < 8
        if self.has_alpha:
            rgb = bleed(rgb, self.alpha)
        self.orig_rgb = rgb
        mode = 'wrap' if (set_name != 'stills' and not self.has_alpha) else ('edge' if self.has_alpha else 'reflect')
        self.pad = PAD
        self.padded = np.pad(rgb, ((PAD, PAD), (PAD, PAD), (0, 0)), mode=mode)

    def finish(self, up_rgb):
        """Crop padding, add the alpha channel back, return a PIL image."""
        p = self.pad * SCALE
        rgb = up_rgb[p:p + self.h * SCALE, p:p + self.w * SCALE]
        rgb8 = (np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)
        if not self.has_alpha:
            return Image.fromarray(rgb8, 'RGB')
        a = Image.fromarray((self.alpha * 255 + 0.5).astype(np.uint8), 'L')
        a = a.resize((self.w * SCALE, self.h * SCALE), Image.LANCZOS)
        return Image.merge('RGBA', (*Image.fromarray(rgb8, 'RGB').split(), a))


def collect_items(quick):
    items = []
    tex_dir = os.path.join(ORIG, 'textures')
    seen = set()
    for fn in sorted(os.listdir(tex_dir)):
        if fn.endswith('.png'):
            h = fn[:-4].rsplit('_', 1)[-1]
            items.append(Item('textures', h, os.path.join(tex_dir, fn))); seen.add(h)
    labels = {i.key: os.path.basename(i.path)[:-4] for i in items}
    for fn in sorted(os.listdir(os.path.join(ORIG, 'stills'))):
        if fn.endswith('.png'):
            items.append(Item('stills', fn[:-4], os.path.join(ORIG, 'stills', fn)))
    # every texture used by the render rooms (so the room renders are fully upscaled)
    idx = json.load(open(os.path.join(TEXDIR, 'textures.json')))
    rooms = {r[1] for r in RENDERS}
    for e in idx:
        if e['bank'] in rooms and e['hash'] not in seen and os.path.exists(os.path.join(TEXDIR, e['hash'] + '.png')):
            items.append(Item('room', e['hash'], os.path.join(TEXDIR, e['hash'] + '.png'))); seen.add(e['hash'])
    if quick:
        items = [i for i in items if i.set == 'textures'][:4] + [i for i in items if i.set == 'stills'][:1]
    return items, labels


# ---------------------------------------------------------------- inference
def load_model(path, device, half):
    import torch
    from spandrel import ModelLoader
    desc = ModelLoader().load_from_file(path)
    dtype = torch.float32
    if half:
        dtype = torch.float16 if desc.supports_half else (torch.bfloat16 if desc.supports_bfloat16 else torch.float32)
    desc.to(device).eval()
    desc.model.to(dtype)
    return desc, dtype


def run_model(desc, dtype, device, img, tile=192, overlap=24):
    """img: HxWx3 float32 0..1 -> (H*s)x(W*s)x3, tiled with feathered overlaps for large inputs."""
    import torch
    s = desc.scale
    H, W = img.shape[:2]

    def infer(x):
        t = torch.from_numpy(np.ascontiguousarray(x.transpose(2, 0, 1)))[None].to(device, dtype)
        with torch.inference_mode():
            y = desc(t)
        return y[0].float().clamp(0, 1).cpu().numpy().transpose(1, 2, 0)

    if H <= tile and W <= tile:
        return infer(img)
    out = np.zeros((H * s, W * s, 3), np.float32); wsum = np.zeros((H * s, W * s, 1), np.float32)
    step = tile - overlap
    ys = list(range(0, max(H - tile, 0) + 1, step)); xs = list(range(0, max(W - tile, 0) + 1, step))
    if ys[-1] + tile < H: ys.append(H - tile)
    if xs[-1] + tile < W: xs.append(W - tile)
    for y in ys:
        for x in xs:
            t = img[y:y + tile, x:x + tile]
            r = infer(t)
            th, tw = t.shape[:2]
            wy = np.minimum(np.arange(th * s) + 1, np.arange(th * s)[::-1] + 1).astype(np.float32)
            wx = np.minimum(np.arange(tw * s) + 1, np.arange(tw * s)[::-1] + 1).astype(np.float32)
            wgt = np.minimum(np.minimum(wy[:, None], overlap * s), np.minimum(wx[None, :], overlap * s))[..., None]
            out[y * s:(y + th) * s, x * s:(x + tw) * s] += r * wgt
            wsum[y * s:(y + th) * s, x * s:(x + tw) * s] += wgt
    return out / np.maximum(wsum, 1e-6)


def out_path(variant, item):
    return os.path.join(RESULTS, 'variants', variant, item.set, item.key + '.png')


# ---------------------------------------------------------------- renders
def render_job(args):
    rid, room, cam, at, skip, variant, hd_dir, out, bilinear = args
    try:
        import room as roomfmt, render_room
        r = roomfmt.load(os.path.join(ORGDATA, 'room', room + '.bin'))
        if cam is None and len(r.cameras) > 1:  # the room's own first gameplay camera
            cam, at = render_room.game_camera(r.cameras[1].eye, r.cameras[1].target)
        if cam is None:
            xs = [v[0] for v in r.shell.verts]; zs = [v[2] for v in r.shell.verts]
            x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
            cam = (x0 + (x1 - x0) * 0.9, 0, z0 + (z1 - z0) * 0.92)
            at = (x0 + (x1 - x0) * 0.25, 150, z0 + (z1 - z0) * 0.15)
        img = render_room.render(r, cam, at, RENDER_W, RENDER_H, RENDER_FOV, 1, set(skip), 0,
                                 hd_dir, bilinear, RENDER_SSAA if bilinear else 1)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        img.save(out)
        return None
    except Exception:
        return f'{variant}/{rid}: ' + traceback.format_exc()


def render_all(variants, workers):
    jobs = []
    for v in variants:
        hd = None if v in ('original', 'original_bilinear') else [
            os.path.join(RESULTS, 'variants', v, 'textures'), os.path.join(RESULTS, 'variants', v, 'room')]
        for rid, room, cam, at, skip in RENDERS:
            out = os.path.join(RESULTS, 'renders', v, rid + '.png')
            if not os.path.exists(out):
                jobs.append((rid, room, cam, at, skip, v, hd, out, v != 'original'))
    if not jobs:
        return
    log(f'Rendering {len(jobs)} room images on {workers} CPU workers ...')
    t0 = time.time(); done = 0
    try:
        with cf.ProcessPoolExecutor(max_workers=workers) as ex:
            for err in ex.map(render_job, jobs):
                done += 1
                if err:
                    log('  render FAILED', err)
                if done % 10 == 0 or done == len(jobs):
                    el = time.time() - t0
                    log(f'  {done}/{len(jobs)} renders, {el / 60:.1f} min elapsed, ~{el / done * (len(jobs) - done) / 60:.1f} min left')
    except Exception as e:  # e.g. multiprocessing unavailable: fall back to one process
        log('  parallel rendering unavailable (' + str(e) + '); rendering in one process')
        for j in jobs:
            if not os.path.exists(j[7]):
                err = render_job(j)
                if err:
                    log('  render FAILED', err)


# ---------------------------------------------------------------- viewer
def write_viewer(variants, items, labels, model_info):
    sets = {'rooms': [{'id': r[0], 'label': r[0].replace('_', ' ')} for r in RENDERS]}
    for s in ('textures', 'stills'):
        sets[s] = [{'id': i.key, 'label': labels.get(i.key, i.key), 'w': i.w * SCALE, 'h': i.h * SCALE}
                   for i in items if i.set == s]
    for v in variants:  # which sets each variant has images for
        v['has'] = {'rooms': os.path.isdir(os.path.join(RESULTS, 'renders', v['key'])),
                    'textures': os.path.isdir(os.path.join(RESULTS, 'variants', v['key'], 'textures')),
                    'stills': os.path.isdir(os.path.join(RESULTS, 'variants', v['key'], 'stills'))}
    data = {'variants': variants, 'info': model_info, 'sets': sets, 'scale': SCALE}
    with open(os.path.join(RESULTS, 'data.js'), 'w', encoding='utf-8') as f:
        f.write('const DATA = ' + json.dumps(data) + ';\n')
    shutil.copyfile(os.path.join(HERE, 'compare.html'), os.path.join(RESULTS, 'compare.html'))


# ---------------------------------------------------------------- main
def main():
    global LOG
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', nargs='*', default=None, help='model keys to run (default: all)')
    ap.add_argument('--no-renders', action='store_true')
    ap.add_argument('--fp16', action='store_true', help='half precision: faster, slightly lower quality')
    ap.add_argument('--quick', action='store_true', help='smoke test: 2 models, 5 images, no renders')
    ap.add_argument('--workers', type=int, default=max(1, min(8, (os.cpu_count() or 4) - 2)))
    a = ap.parse_args()

    os.makedirs(os.path.join(RESULTS, 'logs'), exist_ok=True)
    LOG = open(os.path.join(RESULTS, 'logs', time.strftime('run-%Y%m%d-%H%M%S.log')), 'w', encoding='utf-8')
    t_start = time.time()

    import torch
    device = 'cuda' if torch.cuda.is_available() else 'cpu'
    log('Doctor Hauzer upscaling test')
    log(f'  torch {torch.__version__}, device: {torch.cuda.get_device_name(0) if device == "cuda" else "CPU (no CUDA GPU found: this will be slow)"}')
    log(f'  project: {PROJECT}')
    log(f'  models:  {MODEL_DIR}')

    models = [m for m in MODELS if (a.only is None or m[0] in a.only)]
    prepasses = PREPASSES
    if a.quick:
        models = models[:2]

    log('\n[1/5] Models')
    paths = {}
    for m in prepasses + models:
        p = fetch(m)
        if p:
            paths[m[0]] = p
    models = [m for m in models if m[0] in paths]
    prepasses = [p for p in prepasses if p[0] in paths]

    log('\n[2/5] Preparing images')
    items, labels = collect_items(a.quick)
    log(f'  {sum(i.set == "textures" for i in items)} test textures, {sum(i.set == "stills" for i in items)} stills, '
        f'{sum(i.set == "room" for i in items)} extra textures for the room renders')

    # baselines
    for it in items:
        for v, res in (('original', Image.NEAREST), ('lanczos', Image.LANCZOS)):
            p = out_path(v, it)
            if not os.path.exists(p):
                os.makedirs(os.path.dirname(p), exist_ok=True)
                Image.open(it.path).convert('RGBA' if it.has_alpha else 'RGB').resize((it.w * SCALE, it.h * SCALE), res).save(p)

    log('\n[3/5] Pre-passes (1x clean-up models)')
    pre = {'none': {id(i): i.padded for i in items}}
    for key, name, *_ in prepasses:
        desc, dtype = load_model(paths[key], device, a.fp16)
        t0 = time.time()
        pre[key] = {id(i): (i.padded if i.tiny else run_model(desc, dtype, device, i.padded, tile=128, overlap=16)) for i in items}
        log(f'  {name}: {len(items)} images in {time.time() - t0:.1f} s')
        del desc
        if device == 'cuda':
            torch.cuda.empty_cache()

    log('\n[4/5] Upscaling')
    variants = [{'key': 'original', 'label': 'Original (3DO, nearest)', 'model': '', 'prepass': ''},
                {'key': 'lanczos', 'label': 'Lanczos 4x (no AI)', 'model': '', 'prepass': ''}]
    model_info = {m[0]: {'name': m[1], 'about': m[5]} for m in MODELS + PREPASSES}
    total = len(models) * len(pre) * len(items); done = 0; t0 = time.time()
    for key, name, *_ in models:
        desc = dtype = None
        for pk in pre:
            v = f'{pk}__{key}'
            variants.append({'key': v, 'label': f'{name}' + ('' if pk == 'none' else f' + {pk}'), 'model': key, 'prepass': pk})
            todo = [i for i in items if not os.path.exists(out_path(v, i))]
            done += len(items) - len(todo)
            if todo and desc is None:
                log(f'  loading {name} ...')
                desc, dtype = load_model(paths[key], device, a.fp16)
            tv = time.time()
            for it in todo:
                try:
                    if it.tiny:
                        up = np.asarray(Image.fromarray((pre[pk][id(it)] * 255 + .5).astype(np.uint8)).resize(
                            (it.padded.shape[1] * SCALE, it.padded.shape[0] * SCALE), Image.LANCZOS), np.float32) / 255
                    else:
                        up = None
                        for tile in (192, 96, 64):
                            try:
                                up = run_model(desc, dtype, device, pre[pk][id(it)], tile=tile, overlap=tile // 8)
                                break
                            except RuntimeError as e:  # CUDA out of memory: retry with smaller tiles
                                if 'out of memory' not in str(e).lower() or tile == 64:
                                    raise
                                torch.cuda.empty_cache()
                                log(f'  (out of GPU memory on {it.key}; retrying with {tile // 2}px tiles)')
                    p = out_path(v, it); os.makedirs(os.path.dirname(p), exist_ok=True)
                    it.finish(up).save(p)
                except Exception as e:
                    log(f'  FAILED {v} {it.set}/{it.key}: {e}')
                    if device == 'cuda':
                        torch.cuda.empty_cache()
                done += 1
            if todo:
                el = time.time() - t0
                log(f'  {v}: {len(todo)} images in {time.time() - tv:.1f} s   [{done}/{total}, ~{el / max(done, 1) * (total - done) / 60:.1f} min left]')
        del desc
        if device == 'cuda':
            torch.cuda.empty_cache()

    vkeys = [v['key'] for v in variants]
    if not a.no_renders and not a.quick:
        log('\n[5/5] Room renders (1920x1080, bilinear filtering, 2x supersampling)')
        variants.insert(1, {'key': 'original_bilinear', 'label': 'Original textures, bilinear', 'model': '', 'prepass': ''})
        render_all(['original', 'original_bilinear'] + vkeys[1:], a.workers)
    else:
        log('\n[5/5] Room renders skipped')

    write_viewer(variants, items, labels, model_info)
    log(f'\nFinished in {(time.time() - t_start) / 60:.1f} min. Open: {os.path.join(RESULTS, "compare.html")}')


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        log('Stopped. Run again to continue where it left off.')
    except Exception:
        log('ERROR:\n' + traceback.format_exc())
        sys.exit(1)
