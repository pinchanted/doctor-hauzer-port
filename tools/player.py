#!/usr/bin/env python3
"""Player control, movement and collision: a faithful model of launchme's code.

Sources (launchme addresses):
  0x5578  per-tick input handler: reads the 3DO control pad and picks a state
  0x5aa4  state switch (16 states), each one tick of movement + animation
  0x12648 / 0x1256c  move an actor along its facing (vector rotated by the actor's angles,
          components optionally zeroed, fraction truncated: floor of the 16.16 result)
  0x10ee0 collision: first solid volume (type 1) in the current area overlapping the actor
  0x109d4 / 0x10b40  AABB overlap tests (centre distance < sum of half sizes on all axes);
          the first returns the other volume's solid kind, the second its trigger id
  0x10fb4 floor trigger under the player (box shrunk to +-30 while testing)
  0x110fc probe: smallest trigger id overlapping (used 80 units ahead for examine / use item)
  0x12ae0 grid volumes: 20x20 cells of 200 units, x = -2000 + col*200, z = 2000 - row*200
          (row-major), y -200..200; layer-1 value 99 = wall (solid), layer-2 value = trigger id
  0x10d28 object volumes: bounding box of the mesh's *local* vertices (rotation ignored)
          around the object's position; solid kind = record +0x70 (2 = pushable), trigger = +0x74

Units: room units, 256 angle units per turn, floor at y = +200, one tick = one rendered frame
(the game loop has no vertical-blank wait, so the original speed varied with scene load).

Collision volume record (0x5c498, 40 bytes per actor handle; rooms use handles 100+):
  +0x00 type (1 = solid)  +0x04 actor  +0x08..+0x1c min x,y,z / max x,y,z (16.16, actor-relative)
  +0x20 trigger id  +0x24 solid kind (returned on overlap; 0 = no hit)
"""
import math
import os
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# 3DO ControlPad bits (pad word at 0x33ed8)
DOWN, UP, RIGHT, LEFT = 0x80000000, 0x40000000, 0x20000000, 0x10000000
A, B, C, START = 0x08000000, 0x04000000, 0x02000000, 0x01000000
X, RSHIFT, LSHIFT = 0x00800000, 0x00400000, 0x00200000

WALK, RUN, JUMP_STEP, PROBE = 14, 28, 28, 80
TURN = 8                      # angle units per tick (11.25 degrees)
PLAYER_BOX = (-60, -270, -60, 60, 0, 60)
FLOOR_BOX = (-30, -270, -30, 30, 0, 30)

# states (argument of 0x5aa4) -> what they do
STATES = {
    0: 'idle (anim slot 2)', 1: 'walk forward 14/tick, push a pushable at 12/tick',
    2: 'walk back 14/tick', 3: 'jump: 8 ticks wind-up, 12 ticks at 28/tick, 4 ticks landing',
    4: 'turn right (-8)', 5: 'turn left (+8)', 6: 'examine: pose 5 ticks, probe 80 ahead, fire event 2',
    7: 'run forward 28/tick, push a pushable at 18/tick', 8: 'pull a pushable / step back 14/tick',
    9: 'hold pose (slot 10)', 10: 'hold pose (slot 11)', 11: 'reach (slot 12)', 12: 'collapse (slot 13)',
    13: 'use item: probe 80 ahead, fire event 3 with the item', 14: 'hold pose (slot 15)', 15: 'hold pose (slot 16)',
}


@dataclass
class Box:
    lo: tuple            # world-space min x, y, z
    hi: tuple
    solid: int = 0       # 0 none, 99 wall, 1 solid, 2 pushable
    trigger: int = 0
    owner: object = None  # RoomObject for object volumes

    def overlaps(self, o):
        return all(self.lo[i] < o.hi[i] and o.lo[i] < self.hi[i] for i in range(3))


def actor_box(pos, rel):
    return Box(tuple(pos[i] + rel[i] for i in range(3)), tuple(pos[i] + rel[3 + i] for i in range(3)))


def room_volumes(rm, area=0):
    """All collision volumes of a room (grid walls and triggers, then objects of `area`)."""
    vols = []
    for row in range(20):
        z1 = 2000 - row * 200
        for col in range(20):
            k = row * 20 + col
            zone, trig = rm.zones[k], rm.triggers[k]
            if zone != 99 and trig == 0:
                continue
            x0 = -2000 + col * 200
            vols.append(Box((x0, -200, z1 - 200), (x0 + 200, 200, z1), 99 if zone == 99 else 0, trig))
    for o in rm.objects[1:]:
        if o.area != area or not o.mesh.verts:
            continue
        if not (o.collidable or o.trigger):
            continue
        vs = o.mesh.verts
        lo = tuple(min(v[i] for v in vs) + o.pos[i] for i in range(3))
        hi = tuple(max(v[i] for v in vs) + o.pos[i] for i in range(3))
        vols.append(Box(lo, hi, o.collidable, o.trigger, o))
    return vols


def zone_at(rm, x, z):
    """Camera-zone value (layer 1) of the grid cell under (x, z); 99 = wall, None outside."""
    col, row = math.floor((x + 2000) / 200), math.floor((2000 - z) / 200)
    if 0 <= col < 20 and 0 <= row < 20:
        return rm.zones[row * 20 + col]
    return None


class Player:
    def __init__(self, rm, pos, ry=0, area=0):
        self.rm, self.area = rm, area
        self.pos = list(pos)
        self.ry = ry
        self.vols = room_volumes(rm, area)
        self.anim = 2
        self.events = []      # (kind, trigger id, extra) fired this tick: 1 floor, 2 examine, 3 use item
        self.busy = 0         # ticks left of a blocking action (jump / examine)

    # -- primitives ------------------------------------------------------------------------------
    def _vec(self, d):
        a = self.ry * 2 * math.pi / 256
        # the game rotates (0, 0, d) by the actor's angles; forward moves pass d < 0 (model faces -z)
        return (d * math.sin(a), 0.0, d * math.cos(a))

    def _move(self, d, use_x=True, use_z=True):
        vx, _, vz = self._vec(d)
        if use_x:
            self.pos[0] += math.floor(vx)
        if use_z:
            self.pos[2] += math.floor(vz)

    def hit(self, pos=None):
        """0x10ee0: first solid volume overlapping the player box."""
        b = actor_box(pos or self.pos, PLAYER_BOX)
        for v in self.vols:
            if v.solid and v.overlaps(b):
                return v
        return None

    def probe(self, dist=PROBE):
        """0x110fc at `dist` ahead: smallest overlapping trigger id, or 0."""
        save = list(self.pos)
        self._move(-dist)
        b = actor_box(self.pos, PLAYER_BOX)
        ids = [v.trigger for v in self.vols if v.trigger and v.overlaps(b)]
        self.pos = save
        return min(ids) if ids else 0

    def floor_trigger(self):
        """0x10fb4: trigger id of a volume under the shrunken player box, or 0."""
        b = actor_box(self.pos, FLOOR_BOX)
        for v in self.vols:
            if v.trigger and v.overlaps(b):
                return v.trigger
        return 0

    def _slide(self, d):
        """The walk/run pattern: try the full move only to detect a pushable, restore, then move
        x alone and z alone, undoing whichever collides (wall sliding)."""
        save = list(self.pos)
        self._move(d)
        h = self.hit()
        self.pos = save
        if h is not None and h.solid == 2:
            return h
        for ux, uz in ((True, False), (False, True)):
            s = list(self.pos)
            self._move(d, ux, uz)
            if self.hit():
                self.pos = s
        return None

    def _push(self, box, step):
        """Move the player and the pushable `box` together by `step`; undo both if either collides."""
        save, lo, hi = list(self.pos), box.lo, box.hi
        vx, _, vz = self._vec(step)
        dx, dz = math.floor(vx), math.floor(vz)
        box.lo, box.hi = (lo[0] + dx, lo[1], lo[2] + dz), (hi[0] + dx, hi[1], hi[2] + dz)
        self.pos[0] += dx; self.pos[2] += dz
        others = [v for v in self.vols if v is not box]
        bad = any(v.solid and v.overlaps(box) for v in others) or \
            any(v.solid and v.overlaps(actor_box(self.pos, PLAYER_BOX)) for v in others)
        if bad:
            self.pos, box.lo, box.hi = save, lo, hi
            return False
        if box.owner is not None:
            box.owner.pos = (box.owner.pos[0] + dx, box.owner.pos[1], box.owner.pos[2] + dz)
        return True

    # -- the state switch (0x5aa4) ---------------------------------------------------------------
    def state(self, s, item=0):
        if s == 0:
            self.anim = 2
        elif s == 1:
            self.anim = 3
            h = self._slide(-WALK)
            if h is not None:
                self.anim = 7
                self._push(h, -12)
        elif s == 2:
            self.anim = 4
            save = list(self.pos)
            self._move(WALK)
            if self.hit():
                self.pos = save
        elif s == 3:
            self.anim = 5
            self.busy = 24
        elif s == 4:
            self.ry = (self.ry - TURN) % 256
        elif s == 5:
            self.ry = (self.ry + TURN) % 256
        elif s == 6:
            self.anim = 9
            t = self.probe()
            if t:
                self.events.append((2, t, 0))
        elif s == 7:
            self.anim = 6
            h = self._slide(-RUN)
            if h is not None:
                self.anim = 7
                self._push(h, -18)
        elif s == 8:
            self.anim = 8
            save = list(self.pos)
            self._move(-36)
            h = self.hit()
            self.pos = save
            if h is not None and h.solid == 2:
                self._push(h, WALK)
            else:
                self._move(WALK)
                if self.hit():
                    self.pos = save
        elif s in (9, 10, 11, 12):
            self.anim = s + 1
        elif s == 13:
            t = self.probe()
            if t:
                self.events.append((3, t, item))
        elif s == 14:
            self.anim = 15
        elif s == 15:
            self.anim = 16

    def _jump_tick(self):
        """Ticks of state 3 after the first: 8 standing, 12 moving 28/tick (undo on a hit), 4 landing."""
        n = 24 - self.busy
        if 8 <= n < 20:
            save = list(self.pos)
            self._move(-JUMP_STEP)
            if self.hit():
                self.pos = save
        self.busy -= 1

    # -- the input handler (0x5578), minus menus -------------------------------------------------
    def tick(self, pad):
        self.events = []
        if self.busy:
            self._jump_tick()
        else:
            acted = False
            if pad & A and not pad & (UP | DOWN):
                self.state(6); acted = True
            if pad & B:
                for bit, s in ((UP, 7), (DOWN, 8), (RIGHT, 4), (LEFT, 5)):
                    if pad & bit:
                        self.state(s); acted = True
            if pad & C:
                self.state(3); acted = True
            elif not acted and pad & RSHIFT:
                for bit, s in ((UP, 10), (DOWN, 9), (RIGHT, 14), (LEFT, 15)):
                    if pad & bit:
                        self.state(s); acted = True
            if not acted:
                if pad & RIGHT:
                    self.anim = 1; self.state(4)
                elif pad & LEFT:
                    self.anim = 1; self.state(5)
                elif pad & UP:
                    self.state(1)
                elif pad & DOWN:
                    self.state(2)
                else:
                    self.state(0)
        t = self.floor_trigger()
        if t:
            self.events.append((1, t, 0))
        return self.events


def topdown(rm, paths, out, area=0):
    """Top-down map: grid walls, trigger cells, object volumes, and player paths."""
    from PIL import Image, ImageDraw
    S = 0.2
    im = Image.new('RGB', (800, 800), (20, 20, 26))
    d = ImageDraw.Draw(im)
    P = lambda x, z: ((x + 2000) * S, (2000 - z) * S)
    for v in room_volumes(rm, area):
        a, b = P(v.lo[0], v.hi[2]), P(v.hi[0], v.lo[2])
        col = (110, 110, 120) if v.solid == 99 else (200, 120, 40) if v.solid == 2 else \
            (70, 90, 160) if v.solid else (40, 110, 60)
        d.rectangle([a, b], fill=col if v.owner is None else None, outline=col)
        if v.trigger:
            d.text((a[0] + 2, a[1] + 2), str(v.trigger), fill=(255, 255, 255))
    for pts, colr in paths:
        d.line([P(x, z) for x, z in pts], fill=colr, width=2)
        d.ellipse([P(*pts[0])[0] - 4, P(*pts[0])[1] - 4, P(*pts[0])[0] + 4, P(*pts[0])[1] + 4], fill=colr)
    im.save(out)


if __name__ == '__main__':
    import room as roomfmt
    path, out = sys.argv[1], sys.argv[2]
    spawn = (0, 200, 0)
    if len(sys.argv) > 3:
        spawn = tuple(int(v) for v in sys.argv[3:6])
    rm = roomfmt.load(path)
    paths = []
    for k, (start_ry, script) in enumerate([(0, [UP] * 200), (64, [UP] * 200), (128, [B | UP] * 120),
                                             (32, [UP] * 60 + [RIGHT] * 8 + [UP] * 120)]):
        p = Player(rm, spawn, start_ry)
        pts = [(p.pos[0], p.pos[2])]
        for pad in script:
            ev = p.tick(pad)
            pts.append((p.pos[0], p.pos[2]))
            for e in ev:
                print(f'path {k}: event {e} at {p.pos}')
        paths.append((pts, [(255, 220, 0), (0, 220, 255), (255, 80, 200), (120, 255, 120)][k]))
    topdown(rm, paths, out)
