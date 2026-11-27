#!/usr/bin/env python3
"""Rebuild the game's movies in high definition for the engine.

The 3DO movies (OrgData/stream/*DS: Cinepak, about 290x210 at 15 fps, SDX2 audio) become
assets_hd/movies/<NAME>.mpg: every frame upscaled 4x (PBRify family: 4x-PBRify_RPLKSRd_V3 by default,
a fast model; --model pbrify_dat2 for the slower one used on the stills), encoded as MPEG-1 video +
MP2 audio, which the engine decodes on every platform (pl_mpeg). The English fan patch changed no
movie picture (only padding bytes), so one set serves both languages; the engine draws English
captions over the Japanese text baked into OPDS, STDS, MESDS and PADDS (engine/assets/text/movies_en.txt).

Stages (each skips finished work, so the script can be stopped and re-run):
  extract   streams -> work/movies/<set>/<NAME>/f00001.png + audio.wav       (needs ffmpeg)
  upscale   frames  -> work/movies/<set>/<NAME>/4x/f00001.jpg (JPEG q95: ~3 GB in all) (needs torch: MELCHIOR)
  encode    frames + audio -> assets_hd/movies[_en]/<NAME>.mpg              (needs ffmpeg)
  all       the three in order (default)

Options: --only OPDS,GODS   --preview (encode a Lanczos upscale; no AI, for testing the engine)
         --model pbrify_dat2 (slower, sharper)   --fp16
ffmpeg: on PATH, or the copy that comes with imageio-ffmpeg if that is installed.
"""
import argparse, os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import stream_extract as SX

NAMES = ['OPDS', 'SLDS', 'MESDS', 'STDS', 'GODS', 'EDDS', 'TRDS', 'PADDS']
# (set, source root, output folder)
SETS = [('jp', os.path.join(PROJECT, 'extracted', 'fs'), os.path.join(PROJECT, 'assets_hd', 'movies'))]
WORK = os.path.join(PROJECT, 'work', 'movies')


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def ffmpeg():
    exe = shutil.which('ffmpeg')
    if exe:
        return exe
    try:
        import imageio_ffmpeg
        return imageio_ffmpeg.get_ffmpeg_exe()
    except Exception:
        sys.exit('ffmpeg not found: put ffmpeg on PATH or pip install imageio-ffmpeg')


def jobs(only):
    """[(set, name, stream path, work dir, output .mpg)]; English movies only where the patch differs"""
    out = []
    for name in NAMES:
        if only and name not in only:
            continue
        jp = os.path.join(SETS[0][1], 'OrgData', 'stream', name)
        for set_, root, dst in SETS:
            src = os.path.join(root, 'OrgData', 'stream', name)
            if not os.path.exists(src):
                continue
            out.append((set_, name, src, os.path.join(WORK, set_, name), os.path.join(dst, name + '.mpg')))
    return out


def extract(j, ff):
    set_, name, src, work, _ = j
    done = os.path.join(work, 'extract.done')
    if os.path.exists(done):
        return
    os.makedirs(work, exist_ok=True)
    from PIL import Image
    d = open(src, 'rb').read()
    fr, _ = SX.frames(d)
    w, h = max(f[1] for f in fr), max(f[2] for f in fr)
    t0, n = fr[0][0], 0
    rs = SX.runs(fr)
    for ri, run in enumerate(rs):                 # each run of one frame size decoded on its own, centred
        rw, rh = run[0][1], run[0][2]
        tmp = tempfile.mkdtemp(prefix='hzmovie_')        # scratch outside the project
        avi = os.path.join(tmp, 'video.avi')
        SX.write_avi(avi, run, rw, rh, hold=ri == len(rs) - 1)
        subprocess.run([ff, '-v', 'error', '-y', '-i', avi, os.path.join(tmp, 'r%05d.png')], check=True)
        k0 = round((run[0][0] - t0) * SX.FPS / SX.TICKS)
        for i, fn in enumerate(sorted(f for f in os.listdir(tmp) if f.endswith('.png'))):
            im = Image.open(os.path.join(tmp, fn)).convert('RGB')
            if im.size != (w, h):
                c = Image.new('RGB', (w, h))
                c.paste(im, ((w - im.size[0]) // 2, (h - im.size[1]) // 2))
                im = c
            im.save(os.path.join(work, 'f%05d.png' % (k0 + i + 1)))
            n = max(n, k0 + i + 1)
        shutil.rmtree(tmp, ignore_errors=True)
    if b'SNDS' in d:
        subprocess.run([ff, '-v', 'error', '-y', '-f', '3dostr', '-i', src, '-ar', '44100', os.path.join(work, 'audio.wav')], check=True)
    open(done, 'w').write(f'{n} {w} {h}\n')
    log(f'  extracted {set_}/{name}: {n} frames {w}x{h}')


def frames_of(work, sub='', ext='.png'):
    d = os.path.join(work, sub) if sub else work
    return sorted(f for f in os.listdir(d) if f.startswith('f') and f.endswith(ext)) if os.path.isdir(d) else []


def upscale(js, model_key, fp16):
    import numpy as np, torch
    from PIL import Image
    sys.path.insert(0, os.path.join(HERE, 'upscale_test'))
    import run_upscale_test as T
    entry = next(m for m in T.MODELS if m[0] == model_key)
    path = T.fetch(entry)
    device = 'cuda' if torch.cuda.is_available() else 'cpu'
    desc, dtype = T.load_model(path, device, fp16)
    log(f'  model {entry[1]} on {device}')
    todo = []
    for j in js:
        work = j[3]
        os.makedirs(os.path.join(work, '4x'), exist_ok=True)
        have = set(f[:-4] for f in frames_of(work, '4x', '.jpg'))
        todo += [(work, f) for f in frames_of(work) if f[:-4] not in have]
    log(f'  {len(todo)} frames to upscale')
    t0, last = time.time(), {}
    for k, (work, f) in enumerate(todo):
        src = os.path.join(work, f)
        raw = open(src, 'rb').read()
        out = os.path.join(work, '4x', f[:-4] + '.jpg')
        if last.get(work, (None,))[0] == raw:          # a repeated frame: reuse the last result
            shutil.copyfile(last[work][1], out)
            continue
        img = np.asarray(Image.open(src).convert('RGB'), dtype=np.float32) / 255.0
        for tile in (256, 192, 128, 64):
            try:
                up = T.run_model(desc, dtype, device, img, tile=tile, overlap=tile // 8)
                break
            except RuntimeError as e:
                if 'out of memory' not in str(e).lower() or tile == 64:
                    raise
                torch.cuda.empty_cache()
        Image.fromarray((np.clip(up, 0, 1) * 255 + 0.5).astype(np.uint8)).save(out + '.tmp', 'JPEG', quality=95, subsampling=0)
        os.replace(out + '.tmp', out)
        last[work] = (raw, out)
        if k % 50 == 49:
            el = time.time() - t0
            log(f'  {k + 1}/{len(todo)}, ~{el / (k + 1) * (len(todo) - k - 1) / 60:.0f} min left')


def encode(j, ff, preview):
    set_, name, src, work, dst = j
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    n, w, h = map(int, open(os.path.join(work, 'extract.done')).read().split())
    up = frames_of(work, '4x', '.jpg')
    if not preview and len(up) < n:
        log(f'  {set_}/{name}: only {len(up)}/{n} frames upscaled; run the upscale stage first (or --preview)')
        return
    inp = os.path.join(work, '4x', 'f%05d.jpg') if not preview else os.path.join(work, 'f%05d.png')
    vf = [] if not preview else ['-vf', f'scale={w * 4}:{h * 4}:flags=lanczos']
    cmd = [ff, '-v', 'error', '-y', '-framerate', '15', '-i', inp]
    wav = os.path.join(work, 'audio.wav')
    if os.path.exists(wav):
        cmd += ['-i', wav, '-c:a', 'mp2', '-b:a', '256k', '-ar', '44100', '-ac', '2']
    # MPEG-1 has no 15 fps: 30 fps with every frame shown twice (costs almost nothing)
    cmd += vf + ['-r', '30', '-c:v', 'mpeg1video', '-q:v', '2', '-g', '30', '-bf', '0', '-pix_fmt', 'yuv420p',
                 '-f', 'mpeg', dst + '.tmp']
    subprocess.run(cmd, check=True)
    if os.path.exists(dst):
        os.remove(dst)
    os.rename(dst + '.tmp', dst)
    log(f'  {set_}/{name} -> {os.path.relpath(dst, PROJECT)} ({os.path.getsize(dst) / 1e6:.0f} MB){" [preview]" if preview else ""}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('stage', nargs='?', default='all', choices=['extract', 'upscale', 'encode', 'all'])
    ap.add_argument('--only', default='')
    ap.add_argument('--preview', action='store_true')
    ap.add_argument('--model', default='pbrify_rplksr')
    ap.add_argument('--fp16', action='store_true')
    a = ap.parse_args()
    js = jobs([x.strip().upper() for x in a.only.split(',') if x.strip()])
    log(f'Doctor Hauzer movies: {len(js)} ({", ".join(s + "/" + n for s, n, *_ in js)})')
    t0 = time.time()
    if a.stage in ('extract', 'all'):
        ff = ffmpeg()
        for j in js:
            extract(j, ff)
    if a.stage in ('upscale', 'all') and not a.preview:
        upscale(js, a.model, a.fp16)
    if a.stage in ('encode', 'all'):
        ff = ffmpeg()
        for j in js:
            encode(j, ff, a.preview)
    log(f'Done in {(time.time() - t0) / 60:.1f} min')


if __name__ == '__main__':
    main()
