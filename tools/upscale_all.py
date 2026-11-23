#!/usr/bin/env python3
"""Production upscale: every texture and still in the game with the chosen model.

Chosen model (Aaron, 2026-11-23): 4x-PBRify_UpscalerDAT2_V1, no pre-pass. See docs/UPREZ.md.

Inputs  (from export_textures.py / img2png.py):  extracted/textures/<hash>.png, extracted/png/**/*.img.png
Outputs (4x masters):  assets_hd/textures/<hash>.png, assets_hd/stills/<folder>/<name>.png
Review:  assets_hd/review/compare.html - all 33 rooms rendered at 1080p (original vs HD), every texture, every still.

Re-running skips finished work. Reuses results from the upscale test where they exist.
Options: --no-renders, --workers N
"""
import argparse, concurrent.futures as cf, json, os, shutil, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, 'upscale_test'))
import numpy as np
from PIL import Image
import run_upscale_test as T  # shares the prep (padding, colour bleed), tiling and download code

MODEL_KEY, PREPASS = 'pbrify_dat2', 'none'
PROJECT = T.PROJECT
TEX_SRC = os.path.join(PROJECT, 'extracted', 'textures')
STILL_SRC = os.path.join(PROJECT, 'extracted', 'png')
OUT = os.path.join(PROJECT, 'assets_hd')
REVIEW = os.path.join(OUT, 'review')
TEST_DONE = os.path.join(T.RESULTS, 'variants', f'{PREPASS}__{MODEL_KEY}')


def log(*a):
    T.log(*a)


def still_id(rel):  # 'isearch/sea01.img.png' -> 'isearch/sea01'
    return rel[:-len('.img.png')] if rel.endswith('.img.png') else rel[:-4]


def collect():
    items = []
    for fn in sorted(os.listdir(TEX_SRC)):
        if fn.endswith('.png'):
            it = T.Item('textures', fn[:-4], os.path.join(TEX_SRC, fn))
            it.out = os.path.join(OUT, 'textures', fn)
            it.reuse = [os.path.join(TEST_DONE, s, fn) for s in ('textures', 'room')]
            items.append(it)
    for root, _, files in os.walk(STILL_SRC):
        for fn in sorted(files):
            if fn.endswith('.png'):
                rel = os.path.relpath(os.path.join(root, fn), STILL_SRC).replace('\\', '/')
                sid = still_id(rel)
                it = T.Item('stills', sid, os.path.join(root, fn))
                it.out = os.path.join(OUT, 'stills', sid + '.png')
                it.reuse = [os.path.join(TEST_DONE, 'stills', sid.replace('/', '_') + '.png')]
                items.append(it)
    return items


def rooms():
    d = os.path.join(T.ORGDATA, 'room')
    return sorted(fn[:-4] for fn in os.listdir(d) if fn.endswith('.bin'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--no-renders', action='store_true')
    ap.add_argument('--workers', type=int, default=max(1, min(8, (os.cpu_count() or 4) - 2)))
    a = ap.parse_args()
    os.makedirs(os.path.join(OUT, 'logs'), exist_ok=True)
    T.LOG = open(os.path.join(OUT, 'logs', time.strftime('upscale-%Y%m%d-%H%M%S.log')), 'w', encoding='utf-8')
    t_start = time.time()

    import torch
    device = 'cuda' if torch.cuda.is_available() else 'cpu'
    log('Doctor Hauzer - production upscale (4x-PBRify_UpscalerDAT2_V1)')
    log(f'  device: {torch.cuda.get_device_name(0) if device == "cuda" else "CPU (no CUDA GPU found: this will be slow)"}')
    entry = next(m for m in T.MODELS if m[0] == MODEL_KEY)
    path = T.fetch(entry)
    if not path:
        log('Could not get the model; stopping.'); return 1

    items = collect()
    todo, reused = [], 0
    for it in items:
        if os.path.exists(it.out):
            continue
        src = next((p for p in it.reuse if os.path.exists(p)), None)
        if src:
            os.makedirs(os.path.dirname(it.out), exist_ok=True)
            shutil.copyfile(src, it.out); reused += 1
        else:
            todo.append(it)
    log(f'  {sum(i.set == "textures" for i in items)} textures, {sum(i.set == "stills" for i in items)} stills; '
        f'{reused} reused from the test run, {len(todo)} to upscale')

    if todo:
        desc, dtype = T.load_model(path, device, False)
        t0 = time.time()
        for n, it in enumerate(todo, 1):
            try:
                if it.tiny:
                    up = np.asarray(Image.fromarray((it.padded * 255 + .5).astype(np.uint8)).resize(
                        (it.padded.shape[1] * T.SCALE, it.padded.shape[0] * T.SCALE), Image.LANCZOS), np.float32) / 255
                else:
                    up = None
                    for tile in (192, 96, 64):
                        try:
                            up = T.run_model(desc, dtype, device, it.padded, tile=tile, overlap=tile // 8)
                            break
                        except RuntimeError as e:
                            if 'out of memory' not in str(e).lower() or tile == 64:
                                raise
                            torch.cuda.empty_cache()
                os.makedirs(os.path.dirname(it.out), exist_ok=True)
                it.finish(up).save(it.out)
            except Exception as e:
                log(f'  FAILED {it.set}/{it.key}: {e}')
            if n % 25 == 0 or n == len(todo):
                el = time.time() - t0
                log(f'  {n}/{len(todo)} upscaled, {el / 60:.1f} min, ~{el / n * (len(todo) - n) / 60:.1f} min left')
        del desc
        if device == 'cuda':
            torch.cuda.empty_cache()

    # ---- review: every room, original textures vs HD, 1080p
    room_ids = rooms()
    if not a.no_renders:
        jobs = []
        for v, hd in (('original', None), ('hd', [os.path.join(OUT, 'textures')])):
            for r in room_ids:
                cam = at = None; skip = []
                if r == 'room001':
                    cam, at, skip = (350, -150, 550), (-150, 100, -300), [105]
                out = os.path.join(REVIEW, 'renders', v, r + '.png')
                if not os.path.exists(out):
                    jobs.append((r, r, cam, at, skip, v, hd, out, True))
        if jobs:
            log(f'Rendering {len(jobs)} room views (1920x1080) on {a.workers} CPU workers ...')
            t0 = time.time(); done = 0
            with cf.ProcessPoolExecutor(max_workers=a.workers) as ex:
                for err in ex.map(T.render_job, jobs):
                    done += 1
                    if err:
                        log('  render FAILED', err)
                    if done % 10 == 0 or done == len(jobs):
                        el = time.time() - t0
                        log(f'  {done}/{len(jobs)}, ~{el / done * (len(jobs) - done) / 60:.1f} min left')

    rel = lambda p: os.path.relpath(p, REVIEW).replace('\\', '/')
    data = {
        'variants': [
            {'key': 'original', 'label': 'Original (3DO)', 'model': '', 'prepass': '', 'has': {'rooms': True, 'textures': True, 'stills': True}},
            {'key': 'hd', 'label': 'HD (PBRify DAT2, 4x)', 'model': MODEL_KEY, 'prepass': '', 'has': {'rooms': True, 'textures': True, 'stills': True}},
        ],
        'info': {MODEL_KEY: {'name': entry[1], 'about': entry[5]}},
        'scale': T.SCALE,
        'sets': {
            'rooms': [{'id': r, 'label': r} for r in room_ids],
            'textures': [{'id': i.key, 'label': f'{i.key}  ({i.w}x{i.h})', 'src': {'original': rel(i.path), 'hd': rel(i.out)}}
                         for i in items if i.set == 'textures'],
            'stills': [{'id': i.key, 'label': i.key, 'src': {'original': rel(i.path), 'hd': rel(i.out)}}
                       for i in items if i.set == 'stills'],
        },
    }
    os.makedirs(REVIEW, exist_ok=True)
    with open(os.path.join(REVIEW, 'data.js'), 'w', encoding='utf-8') as f:
        f.write('const DATA = ' + json.dumps(data) + ';\n')
    shutil.copyfile(os.path.join(HERE, 'upscale_test', 'compare.html'), os.path.join(REVIEW, 'compare.html'))
    n_tex = len(os.listdir(os.path.join(OUT, 'textures')))
    log(f'\nDone in {(time.time() - t_start) / 60:.1f} min: {n_tex} HD textures in {os.path.join(OUT, "textures")}')
    log(f'Review: {os.path.join(REVIEW, "compare.html")}')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        log('Stopped. Run again to continue where it left off.')
    except Exception:
        log('ERROR:\n' + traceback.format_exc())
        sys.exit(1)
