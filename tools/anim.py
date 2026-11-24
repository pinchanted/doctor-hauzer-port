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
Names are ours. walk/walk_back/jog/run are confirmed by code: their wrappers (0x5974, 0x59bc, 0x5a04,
0x590c) fire sub_06418 - most likely the footstep sound - on fixed ticks (walk 2,10; walk_back 6,14;
jog 5,13; run 0,4,8,12). The rest are named from the poses (`anim.py strips`) and are tentative.
The player movement state machine 0x5aa4 selects slots 3,4,5,7,8,9,15 and 16; 0x5578 selects 1.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# (slot, name, shift, loop, keys after the snapshot)
ANIMS = [
    (0,  'idle'         , 4, 2, [1, 1, 2, 1, 3]),
    (1,  'step'         , 3, 1, [4, 5]),
    (2,  'idle_b'       , 4, 2, [1, 1, 2, 1, 3]),
    (3,  'walk'         , 2, 1, [7, 8, 9, 6]),
    (4,  'walk_back'    , 2, 1, [6, 9, 8, 7]),
    (5,  'jog'          , 2, 3, [4, 10, 11, 12, 44, 8]),
    (6,  'run'          , 1, 1, [13, 14, 15, 16]),
    (7,  'push'         , 2, 2, [21, 17, 18, 19, 20]),
    (8,  'climb'        , 2, 1, [22, 24, 23, 25]),
    (9,  'pose_27'      , 3, 2, [27, 27, 27]),
    (10, 'pose_30'      , 3, 1, [30, 30]),
    (11, 'pose_31'      , 3, 1, [31, 31]),
    (12, 'reach'        , 2, 1, [32, 33, 34]),
    (13, 'collapse'     , 4, 1, [35, 36, 37, 38, 39]),
    (14, 'sidestep'     , 2, 1, [40, 41, 42, 43]),
    (15, 'pose_28'      , 3, 2, [28, 28, 28]),
    (16, 'pose_29'      , 3, 2, [29, 29, 29]),
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
