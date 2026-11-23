#!/usr/bin/env python3
"""Pick a representative upscaler test set from exported textures and stills.

usage: make_testset.py <textures dir (export_textures.py output)> <stills png dir (img2png.py output)> <out dir>

Copies ~40 images into <out>/textures and <out>/stills, choosing the most detailed
textures of each common size/format (by luminance variance), plus all player/item
banks' largest textures and a fixed list of key stills. Writes a contact sheet.
"""
import json, os, shutil, sys
from PIL import Image, ImageStat

STILLS = ['menu/title.img.png', 'isearch/sea01.img.png', 'isearch/sea15.img.png', 'isearch/sea22.img.png',
          'images/r001L.img.png', 'images/dialy.img.png', 'images/gameover.img.png', 'window/note.img.png',
          'window/cmd01.img.png', 'search/dialy01.img.png']


def detail(path):
    im = Image.open(path).convert('L')
    return ImageStat.Stat(im).stddev[0] * (im.width * im.height) ** 0.5


def main(tex_dir, stills_dir, out):
    idx = json.load(open(os.path.join(tex_dir, 'textures.json')))
    uniq = {}
    for e in idx:
        uniq.setdefault(e['hash'], e)
    groups = {}
    for h, e in uniq.items():
        if e.get('w', 0) < 32 or e.get('h', 0) < 32:
            continue
        key = 'human' if e['bank'] == 'human' else 'item' if e['bank'] == 'item' else 'room'
        groups.setdefault((key, e['w'], e['h'], e['bpp']), []).append(h)
    picks = []
    for (key, w, h, bpp), hs in sorted(groups.items()):
        hs.sort(key=lambda x: -detail(os.path.join(tex_dir, x + '.png')))
        n = 6 if (key == 'room' and len(hs) > 40) else 3 if key == 'room' else 4
        picks += [(key, x) for x in hs[:n]]
    os.makedirs(os.path.join(out, 'textures'), exist_ok=True)
    os.makedirs(os.path.join(out, 'stills'), exist_ok=True)
    for key, h in picks:
        e = uniq[h]
        shutil.copy(os.path.join(tex_dir, h + '.png'),
                    os.path.join(out, 'textures', f"{key}_{e['w']}x{e['h']}_{e['bpp']}bpp_{h}.png"))
    for s in STILLS:
        p = os.path.join(stills_dir, s)
        if os.path.exists(p):
            shutil.copy(p, os.path.join(out, 'stills', s.replace('/', '_').replace('.img', '')))
    # contact sheet of the textures at 2x nearest
    files = sorted(os.listdir(os.path.join(out, 'textures')))
    cell = 136
    cols = 10
    sheet = Image.new('RGB', (cols * cell, ((len(files) + cols - 1) // cols) * cell), (32, 32, 32))
    for i, f in enumerate(files):
        im = Image.open(os.path.join(out, 'textures', f)).convert('RGBA')
        im.thumbnail((128, 128), Image.NEAREST)
        sheet.paste(im, ((i % cols) * cell + 4, (i // cols) * cell + 4), im)
    sheet.save(os.path.join(out, 'contact_textures.png'))
    print(len(files), 'textures,', len(os.listdir(os.path.join(out, 'stills'))), 'stills ->', out)


if __name__ == '__main__':
    main(*sys.argv[1:4])
