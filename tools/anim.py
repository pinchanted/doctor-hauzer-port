#!/usr/bin/env python3
"""Player animations: decoded from launchme.

Definitions are built at boot by sub_03db4 (called from the human loader 0x3480) into 17 slots of
0x3c bytes at 0x3b960, via the constructor sub_03d58:

  +0x00 target mesh   (handle [0x32058+0x9c], the live body that gets drawn)
  +0x04 shift         each segment lasts 2**shift ticks
  +0x08 active        1
  +0x0c tick count    since start / last loop   (read by sub_04478)
  +0x10 key index
  +0x14 t             0 .. 2**shift-1 within the segment
  +0x18 loop index    key index to jump back to when the list hits -1
  +0x1c key[0]        handle [0x32058+0xa0], a snapshot of the pose at the moment of switching
  +0x20.. key[1..]    body pose handles (= HumanPol mesh numbers), -1 terminated

sub_043ec(n) = SetPlayerAnim: if n differs from the current one, copy the live body into the
snapshot mesh and reset +0x0c/+0x10/+0x14 - so every switch cross-fades from wherever the body was.
sub_04488 = step, once per game tick:
  if t > 2**shift-1: t = 0; key += 1
  if key[key] == -1: key = loop; tick count = 0
  nxt = key+1, or loop if key[key+1] == -1
  body = lerp(key[key], key[nxt], t / 2**shift)       (sub_16068, per vertex, integer)
  t += 1, plus one more while key == 0 (the cross-fade runs at double speed)

Poses here are HumanPol mesh numbers; the snapshot key is written as None.
Names are ours, mostly settled by the movement code (tools/player.py, 0x5aa4):
walk (3), walk_back (4), run (6), push (7), pull (8), jump (5), examine (9), turn (1, played while
turning in place), idle (2, the normal idle; slot 0 is the same list, used before the first input).
Footstep wrappers fire sub_06418 on fixed ticks: walk 0x5974 (2, 10), walk_back 0x59bc (6, 14),
run 0x590c (0, 4, 8, 12), push 0x5a04 (5, 13), pull 0x5a44 (6, 14).
Slots 10/11/15/16 are R+Down/Up/Right/Left holds; 12 reach and 13 collapse come from scripts.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# (slot, name, shift, loop, keys after the snapshot)
ANIMS = [
    (0,  'idle_start'   , 4, 2, [1, 1, 2, 1, 3]),
    (1,  'turn'         , 3, 1, [4, 5]),
    (2,  'idle'         , 4, 2, [1, 1, 2, 1, 3]),
    (3,  'walk'         , 2, 1, [7, 8, 9, 6]),
    (4,  'walk_back'    , 2, 1, [6, 9, 8, 7]),
    (5,  'jump'         , 2, 3, [4, 10, 11, 12, 44, 8]),
    (6,  'run'          , 1, 1, [13, 14, 15, 16]),
    (7,  'push'         , 2, 2, [21, 17, 18, 19, 20]),
    (8,  'pull'         , 2, 1, [22, 24, 23, 25]),
    (9,  'examine'      , 3, 2, [27, 27, 27]),
    (10, 'hold_r_down'  , 3, 1, [30, 30]),
    (11, 'hold_r_up'    , 3, 1, [31, 31]),
    (12, 'reach'        , 2, 1, [32, 33, 34]),
    (13, 'collapse'     , 4, 1, [35, 36, 37, 38, 39]),
    (14, 'sidestep'     , 2, 1, [40, 41, 42, 43]),
    (15, 'hold_r_right' , 3, 2, [28, 28, 28]),
    (16, 'hold_r_left'  , 3, 2, [29, 29, 29]),
]
BY_NAME = {a[1]: a for a in ANIMS}


class Player:
    """Port of the stepper. Feed it a slot; call step() once per tick; pose() gives the
    (key_a, key_b, frac) to blend. key None = the snapshot taken at switch time."""

    def __init__(self, slot=0):
        self.slot = None
        self.set(slot)

    def set(self, slot):
        if slot == self.slot:
            return False
        _, _, self.shift, self.loop, keys = ANIMS[slot]
        self.slot, self.keys = slot, [None] + list(keys) + [-1]
        self.k = self.t = self.ticks = 0
        return True

    def step(self):
        span = 1 << self.shift
        if self.t > span - 1:
            self.t = 0; self.k += 1
        if self.keys[self.k] == -1:
            self.k = self.loop; self.ticks = 0
        nxt = self.k + 1 if self.keys[self.k + 1] != -1 else self.loop
        out = (self.keys[self.k], self.keys[nxt], self.t / span)
        self.t += 2 if self.k == 0 else 1
        self.ticks += 1
        return out


def lerp_mesh(a, b, f):
    import room as roomfmt
    return roomfmt.Mesh([tuple(pa[i] + (pb[i] - pa[i]) * f for i in range(3)) for pa, pb in zip(a.verts, b.verts)],
                        a.faces)


def strips(human_dir, launchme, out):
    """One row per animation: its key poses, labelled."""
    import human, render_room, room as roomfmt
    from PIL import Image, ImageDraw
    mdl = human.Model(human_dir, launchme)
    W, H = 150, 200
    rows = []
    for slot, name, shift, loop, keys in ANIMS:
        row = Image.new('RGB', (160 + W * 6, H), (24, 24, 32))
        d = ImageDraw.Draw(row)
        d.text((6, 8), f'{slot}: {name}', fill=(255, 255, 0))
        d.text((6, 28), f'{1 << shift} ticks/key', fill=(200, 200, 200))
        d.text((6, 48), f'loop -> key {loop}', fill=(200, 200, 200))
        for j, k in enumerate(keys[:6]):
            objs = mdl.assembled(k, rot=(0, 40, 0))
            rm = roomfmt.Room((), objs, mdl.textures, data=mdl.tex_data)
            im = render_room.render(rm, (150, -110, 430), (0, -110, 0), W, H, 45, bilinear=True)
            ImageDraw.Draw(im).text((4, 4), str(k), fill=(255, 255, 255))
            row.paste(im, (160 + j * W, 0))
        rows.append(row)
    S = Image.new('RGB', (rows[0].width, H * len(rows)))
    for i, r in enumerate(rows):
        S.paste(r, (0, i * H))
    S.save(out)


if __name__ == '__main__':
    if len(sys.argv) >= 5 and sys.argv[1] == 'strips':
        strips(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        p = Player(3)
        for _ in range(20):
            print(p.step())
