# Doctor Hauzer — File Formats

All multi-byte values are **big-endian** (3DO ARM60 runs big-endian). Status: ✅ decoded and verified, 🟡 partly understood, ❓ unknown.

## Disc ✅
Opera file system, 2048-byte blocks; every directory and many files have several "avatars" (copies) for seek optimisation. `tools/opera_extract.py`.

## Stills — `.img` (IMAG) ✅
`IMAG` chunk: width, height, bytes-per-row, bpp=16, …, pixel order 1 = **LRform** (each 32-bit word holds a pixel from line 2k and 2k+1), then `PDAT`. RGB555. All 320×240. `tools/img2png.py`.

## CEL images ✅
Standard 3DO CEL chunks: `CCB ` (flags, PIXC, PRE0/PRE1, width, height), optional `PLUT`, `XTRA`, `PDAT`. `tools/cel.py` handles 1/2/4/6/8/16 bpp, coded/uncoded, packed/unpacked.

**AMV shading:** room textures are 8 bpp coded with PIXC primary = `0x3F00` (multiplier select = AMV). Pixel = `PLUT[v & 31] × ((v >> 5) + 1) / 8`. The 32-entry palette × 8 brightness levels work as a **256-colour quantizer**. The brightness bits carry texture detail, not a separate lighting layer (verified: splitting them leaves speckled colour and loses the cracks). Upscale the final decoded colour, after a de-quantization/denoise pass.

## Texture banks ✅
`u32 count`, then `count × (u32 offset, u32 size)` relative to the bank start; each entry is a complete CEL (chunks). Found at:
- room files: bank at header `+0x00`
- `human/HumanTex.bin` (53 textures), `item/ItemTex.bin` (33): bank at file start

638 unique textures across the game (1200 references); mostly 64×128, 64×64, 32×32 at 8 bpp AMV or 16 bpp. `tools/export_textures.py`.

## Meshes ✅
```
+0x00 u32 n_verts
+0x04 u32 n_faces
+0x08 u32 runtime buffer size (= 6*n_verts + 16)
+0x0C 5 × u32 0xFFFFFFFF (runtime pointers)
+0x20 n_verts × (i32 x, i32 y, i32 z)      y points DOWN; floor at y = +200; rooms on a 200-unit grid
then  n_faces × (i32 n, i32 tex, i32 v0 … v[n-1])     VARIABLE length: quads 6 words, triangles 5
```
Verified: all 689 room meshes and 69 player meshes parse to exactly their stated size (14,490 quads, 4,212 triangles, a few 2-vertex faces).
Quad → texture mapping: v0 = top-left, v1 = top-right, v2 = bottom-right, v3 = bottom-left (3DO CEL corner mapping; verified by render). Triangles are drawn as a CEL quad whose last corner collapses onto v2, so the whole texture squeezes into the triangle.

## Room files — `room/roomNNN.bin` ✅
Verified against `launchme`: loader `0x148dc`, object builder `0x149dc`, collision builder `0x12ae0`, renderer `0x12ee4`, collision test `0x10ee0`. Parser: `tools/room.py`.

| Header | Meaning |
|---|---|
| 0x00 | texture bank offset ✅ |
| 0x04 | unused by the loader: a stale build-tool value pointing into the texture bank |
| 0x08 / 0x0C | collision + camera-zone grid ("Atari Table" in the debug strings) / size `0xC80` ✅ |
| 0x10 | camera table ✅ |
| 0x14 | record count hint; 0x1C / 0x20 shell mesh offset / size (same as record 0) |

**Object records** — 0x80 bytes each from **0x18**, until index = −1. Record 0 is the room shell.

| Offset | Meaning |
|---|---|
| +0x00 | object index (100, 101, …) ✅ |
| +0x04 / +0x08 | mesh offset / size ✅ |
| +0x0C | position x, y, z ✅ |
| +0x18 | rotation x, y, z — 256 units per turn (11 objects game-wide, single axis) ✅ |
| +0x24 | texture table, 16 entries: face tex `t` → bank index `table[t]` ✅ |
| +0x64 / +0x68 / +0x6C | billboard bits / draw routine / no-cull (see Rendering) |
| +0x70 | collidable/selectable 🟡 |
| +0x74 | trigger id — matches the script's `ON (id, …)` handlers ✅ |
| +0x78 | **area** ✅ — objects render and collide only while the global current area (`0x36698`) equals this. Area 0 is the room; areas 1+ are extra scenes in the same file. Room 001's areas 1 and 2 are the single- and double-door transition views. 1–5 areas per room. |
| +0x7C | initially visible ✅ (e.g. stacked animation frames, only the first visible) |

**Collision / camera-zone grid** — 20 × 20 cells of 200 units covering x, z ∈ [−2000, 2000]; cell (i, j) spans x = −2000 + 200i … +200, z = 2000 − 200j … −200.
- Layer 1 (400 ints): **camera zone**, i.e. the camera used while the player stands in the cell; 99 = solid.
- Layer 2 (400 ints at +0x640): **trigger id** (script event, e.g. doors and bookshelves); 99 = solid.
- A cell gets a 200 × 400 × 200 collision box when layer 2 ≠ 0 or layer 1 = 99.

**Camera table** — 32-byte entries `(id, eye x, y, z, target x, y, z, 0)`, id = entry index, ending at the first mismatch (3–14 per room). Camera 0 is an overhead view; cameras 1+ match the zone ids in the grid. The bytes after the table are leftover build-tool memory. Field of view not yet confirmed (50° is a visual estimate).

## Player and items — `human/`, `item/` ✅
`HumanPol.bin`: `u32 count` (69), `count × (offset, size)`, then meshes (standard format).

| Meshes | Content |
|---|---|
| 0–46 | full-body poses (vertex animation; 91 verts / 96 faces each), sequenced by the animation table below |
| 47–56 | close-up heads with expressions (cutscenes); 49 back of head, 50 hair |
| 57 | in-game head (51 verts) |
| 58 / 59 | left / right hand |
| 60–63 | YES/NO choice panels (3D UI) |
| 64–68 | held items: candelabra, knife, rod, … |

**Rigid parts on a vertex-animated body:** every body pose has three thin marker triangles (verts 62–64 left hand, 65–67 right hand, 68–70 head; table entry 14) giving *(attach point, forward, up)*. The engine places the head/hand meshes on them each frame: local +x → right, +y → −up, +z → −forward. Implemented in `tools/human.py`.

**Texture tables** for the player and item meshes live in `launchme`'s data (`0x322FC`–`0x32BFC` player, `0x32BFC`+ items): 16 `(type, value)` pairs per mesh. Type 1 = room texture bank (rooms convert their single-word tables to this), **2 = `HumanTex.bin`**, **3 = `ItemTex.bin`**, 4 = TBD, 0 = none. Mesh → table mapping from the loader at `0x3480` (`TABLE_FOR_MESH`).

### Items, inventory and menus ✅
- **Models:** `item/ItemPol.bin` holds 35 meshes; the loader at `0x32E4` puts mesh *k* (texture table `0x32BFC + 0x80·k`) in actor handle `220 + k`. **Item id = mesh index** (mesh 0 is a dummy; item 35, the pot, has no mesh).
- **Item records** at `0x35C8C`, 56 bytes per id: handle (220 + id), idle spin x/y/z (added to the rotation every menu frame, 256 per turn), then the Shift-JIS name (40 bytes). Ids: 1–10 keys, 11–14 Hauzer's diary 1–4, 15–16 memos, 17–18 floor maps, 19 dictionary, 20 leather book, 21–35 objects (28 = the notebook, which saves).
- **Examine pages:** `0x34894`, 32 bytes per id = up to 8 indices into the page table at `0x34100` (38-byte paths `isearch/seaNN.img`), −1 terminated.
- **Inventory:** 40 ids at `0x3E088`, packed from slot 0; cursor at `0x32064`. `0xE398` has, `0xE344` add, `0xE424` remove (shifts down, cursor −1).
- **Start menu (`0xF26C`):** background `window/cmd01.img`; the item turns in the spotlight at (0, −50, 400) in front of a camera at the origin. Left/Right change item, Up/Down pick 使う (use) / 調べる (examine), A confirms, B/C cancel. SE 3 open, 5 move, 4 cancel. Examine slides the model to x = 140 beside the page; on the page the d-pad and L/R change the spin speed (±0.25 per frame), B stops it, A turns the page. Examining the leather book (20) while `g105 = 0` offers the bronze key (5) afterwards. Empty inventory: 持ち物は何もない.
- **Using an item (`0x55BC`):** item 28 opens the save notebook (`0xDCC8`). Otherwise the player state 13 probes 80 units ahead and fires `(trigger, 3, item)`, then the event `(item + 100, 0, 0)` fires; if no script handler ran, message 400 (ids < 11, "this key doesn't seem to work here") or 401 is shown for 16.
- **Script commands:** `IF_ITEM item, L` (`0xE398`) · `ITEM_SLOT item` = remove the used item (`0xE424`) · `ITEM_SCREEN item, L` (`0xECD4`): "〜を見つけました / 取りますか？" with YES/NO; yes → SE 6, add the item, `g[100 + item] = 1` when `g105 = 0`, jump to L; no → SE 4, fall through. Scripts keep each item's state in `g[100 + item]` (0 not found, 1 held, 2 used).
- **`PICTURE n`** (`0x6CE8` → `0x19B4`): line *n* (0-based) of `images/image.scp`, faded in over 10 frames, held until a button, faded out. Room 001 uses 6/7 (`r001L`/`r001R`).
- `FC60` / `FC61` set a main-loop mode word (`0x35460 + 0x3C`); not needed by the port so far.

### Player animations ✅
Defined in code, not in a file: `sub_03db4` builds 17 animation slots of 0x3C bytes at `0x3B960`. Pose handles are HumanPol mesh numbers (the loader puts mesh *k* in slot *k* of the handle array at `0x3C4A0`).

| Offset | Field |
|---|---|
| +0x00 | target mesh (the live body that is drawn) |
| +0x04 | `shift`: each key-to-key segment lasts 2^shift ticks |
| +0x08 | active flag |
| +0x0C | tick count since start or last loop (footstep sounds key off this) |
| +0x10 | current key index |
| +0x14 | `t`, 0 … 2^shift − 1 |
| +0x18 | loop index: the key to jump back to at the −1 terminator |
| +0x1C | key 0: a snapshot of the pose at the moment of switching |
| +0x20… | keys 1…: body poses, −1 terminated |

- **Switching** (`sub_043ec`, SetPlayerAnim): if the slot changes, copy the live body into the snapshot and reset the counters. Every change cross-fades from wherever the body was. The cross-fade segment runs at double speed.
- **Stepping** (`sub_04488`, once per tick): linear per-vertex blend between the current and next key (`sub_16068`). Wraps to the loop index, so key 1 (and key 2 where loop = 3) is a one-time lead-in.

| Slot | Name | Ticks/key | Loop → | Poses |
|---|---|---|---|---|
| 0 | idle_start | 16 | 2 | 1 1 2 1 3 |
| 1 | turn (in place) | 8 | 1 | 4 5 |
| 2 | idle | 16 | 2 | 1 1 2 1 3 |
| 3 | walk | 4 | 1 | 7 8 9 6 |
| 4 | walk_back | 4 | 1 | 6 9 8 7 |
| 5 | jump | 4 | 3 | 4 10 11 12 44 8 |
| 6 | run | 2 | 1 | 13 14 15 16 |
| 7 | push | 4 | 2 | 21 17 18 19 20 |
| 8 | pull | 4 | 1 | 22 24 23 25 |
| 9 | examine | 8 | 2 | 27 27 27 |
| 10 | hold_r_down | 8 | 1 | 30 30 |
| 11 | hold_r_up | 8 | 1 | 31 31 |
| 12 | reach | 4 | 1 | 32 33 34 |
| 13 | collapse | 16 | 1 | 35 36 37 38 39 |
| 14 | sidestep (unused by the player code) | 4 | 1 | 40 41 42 43 |
| 15 | hold_r_right | 8 | 2 | 28 28 28 |
| 16 | hold_r_left | 8 | 2 | 29 29 29 |

**How the names were settled:**
- **From the movement code:** most names come from `tools/player.py`.
- **Footstep timing:** each gait's footstep wrapper fires `sub_06418` on fixed ticks:

  | Gait | Wrapper | Footstep ticks |
  |---|---|---|
  | walk | `0x5974` | 2, 10 |
  | walk_back | `0x59BC` | 6, 14 |
  | run | `0x590C` | 0, 4, 8, 12 |
  | push | `0x5A04` | 5, 13 |
  | pull | `0x5A44` | 6, 14 |

- **From scripts:** reach (12) and collapse (13) are triggered by scripts. Slot 14 is not used by the player code.

**Implementation:** `tools/anim.py` holds the table, a faithful stepper (`Player`) and `strips` (a contact sheet of each animation's keys).

**Port note:** the engine should blend in floating point at the display rate (60 fps). The integer `t >> shift` steps of the original then become smooth motion.

### Player control, movement and collision ✅
Reverse-engineered from `0x5578` (input), `0x5AA4` (16-state switch) and `0x10EE0`/`0x109D4`/`0x10B40` (collision). Modelled in `tools/player.py`.

**Controls** (3DO pad bits, pad word at `0x33ED8`). B is a "run/handle" modifier and R a "pose" modifier.

| Input | State → effect |
|---|---|
| Up / Down | 1 walk 14 units/tick / 2 walk back 14 |
| Right / Left | 4 / 5 turn ∓8 of 256 per tick (11.25°), turn animation |
| B + Up | 7 run 28/tick; pushes a pushable at 18/tick |
| B + Down | 8 pull: drags a pushable within 36 units ahead back 14/tick, else steps back |
| B + Right/Left | turn without the turn animation |
| A (without Up/Down) | 6 examine: probe 80 ahead; fires event (trigger, 2, 0) |
| C | 3 jump: 8 ticks wind-up, 12 ticks at 28/tick, 4 ticks landing (336 units) |
| R + direction | hold poses 9/10/14/15 |
| Start | item menu (`0xF26C`); using an item fires (trigger 80 ahead, 3, item) → state 13 |
| X | controls screen (`images/operation.img`) |
| L | `sub_01814` + `sub_04a9c` (TBD) |
| Nothing | idle |

**Walking into a pushable** (state 1) switches to the push animation and moves it at 12/tick.

**Movement:**
- **Direction:** the move vector `(0, 0, d)` is rotated by the actor's angles (math folio `SWI 0x50000`). Each component is truncated to whole units (floor), then added to the 16.16 position.
- **Wall sliding (walk/run):** the full move is tried first only to detect a pushable. Then the position is restored, and the x-only and z-only moves are applied separately, each undone if it collides.

**Collision volumes** are axis-aligned boxes, stored 40 bytes per actor handle at `0x5C498`:

| Offset | Field |
|---|---|
| +0 | type (1 = solid) |
| +4 | actor |
| +8…+0x1C | min/max x, y, z (16.16, actor-relative) |
| +0x20 | trigger id |
| +0x24 | solid kind returned on overlap: 0 none, 1 solid, 2 pushable, 99 wall |

Overlap: centre distance is less than the sum of half sizes, on every axis.

- **Player:** box ±60 × 270 tall (feet at y = +200). It shrinks to ±30 for the floor-trigger check (`0x10FB4`), which fires event (trigger, 1, 0) every tick.
- **Walls and floor triggers** (`0x12AE0`): one box per grid cell of 200 units. Cell x = −2000 + col·200, z = 2000 − row·200, y −200…200. Layer 1 = 99 makes a wall; a non-zero layer 2 sets the cell's trigger id.
- **Room objects** (`0x10D28`, handles 100+): box = bounds of the mesh's *local* vertices, ignoring rotation, around the object position. Solid kind = record +0x70; trigger = record +0x74. Only objects in the current area count.
- **Script hooks:** events match script `ON (trigger, kind, item)` lines. For example, room 001's `ON (31, 2, 0)` = examine the door with trigger 31; verified in the simulation.

**Timing:** the gameplay loop has no vertical-blank wait (`DisplayScreen` flips as soon as a frame is drawn), so one tick = one rendered frame and the original speed varied with scene load. The port uses a fixed, configurable logic rate, to be calibrated against gameplay footage.

**Open questions:**
- Room 001's `ON (41, 1, 0)` (floor event under the chandelier) can't fire with the chandelier hanging (box y −366…−242). It is probably reached after a script moves the chandelier.

`RGB.bin` is a "mato" (*matome*, bundle) file: three tables of 16 RGB colours (5-bit), not used by the textured player; purpose TBD (likely lighting or effect colours).

## Rendering and camera ✅ (projection) / 🟡 (draw paths)
- **Projection** (`0x102BC`, called once at boot with `(−64, 192)`): a 16.16 reciprocal table at `0x72B58` (8000 entries) gives `screen = v · 256 / (z + 64) + centre`.
  - The screen is 320×240; z is camera-space depth from the camera position, forward +z.
  - So the focal length is **256 px**, the true eye is **64 units behind the camera position**, and the vertical FOV is **2·atan(120/256) = 50.19°** (64.0° horizontal).
  - Points closer than z = 192 use a linear approximation instead of 1/d. Actors whose centre is behind z = −364 are culled.
  - `tools/render_room.py` reproduces this (`GAME_FOV`, `game_camera()`).
- **Draw loop** (`0x12EE4`): every visible actor in the current area is drawn. Actor fields +0x24/+0x28/+0x2C are copied from room-record +0x64/+0x68/+0x6C:

  | Actor field | Meaning |
  |---|---|
  | +0x24 | billboard bits (1/2/4: copy the camera's x/y/z rotation so the object always faces the camera) |
  | +0x28 | draw routine (0 → `0x13C50`, else `0x14118`) |
  | +0x2C | skip the distance/visibility cull |

- **Camera:** the camera is an actor (handle at `0x36698+0x2E8`). `CAMERA n` copies a room camera's eye and target into it and aims it (`0x12C2C` → `0x11B34`).

## Text — `mes/mes.scp` ✅
- **Format:** Shift-JIS, one message per line, separated by CR. `MESSAGE n` shows line n; line 0 is a dummy.
  - Ids **400+** come from 32-byte Shift-JIS strings inside launchme at `0x33FEC + (n − 400)·32`, e.g. 400 "this key can't be used here", 401 "no point using it here".
  - `mes_no.scp` is the same list with line numbers; `MESSAGE*.TXT` are the writers' annotated scripts, grouped by room and object.
- **Display** (`0x4868` queue / `0x48FC` pop / `0x4A28` per frame): 32 queued (id, time) pairs.
  - `MESSAGE id, time, 1` clears the queue first; `…, 0` appends.
  - Each frame the countdown drops by one; at 0 the next pair is popped. Time 0 means the queue is empty and the box closes; id 0 is a pause without text.
  - Time is counted in rendered frames. The engine scales it assuming about 10 fps.

## Sound ✅
- **Effects:** `effect/*` are AIFF, 8-bit signed mono at about 11 kHz.
  - `SE n` plays the fixed table at `0x35598`: 0 `ASHI_YUKA1` (default footstep), 1 `AKERU1` (door opens), 2 `SIMERU1` (door closes), 3 `KETTEI`, 4 `CANCEL`, 5 `SENTAKU` (menu), 6 `ITEMGET`, 7 `SAKEBI` (scream).
  - Room sounds go through slots:
    - `SE_LOAD slot, n, ?` loads `effect/Geffect.list` entry n (0-based, CR-separated);
    - `SE_PLAY slot` (`0xB91C`), `SE_FREE slot` (`0xB164`), `SE_WAIT slot` (`0xB2A0`).
  - `FOOTSTEP slot` (`0x63F8`) makes footsteps play that slot instead of the default; footsteps fire on fixed animation ticks.
- **Music:** `music/*.aifc` are stereo 22,050 Hz, AIFC with SDX2 compression.
  - Each byte b decodes to `b·|b|·2`, added to the previous sample when b is odd.
  - `BGM n` picks track n from launchme's list: 1–6 `music001`–`006`, 7 `tokei_007`, 8 `inu_008`, 9 `iwa_009`, 10 `mizu_010`, 11 `FUKUROU`, 12 `ending`. 0 = stop. Tracks loop.
- **End of a room loop** (`0xC90`):
  - flag `0x32058+0x74` = ending: plays the `stream/EDDS` movie, then the credits;
  - flag `+0x6C` (`EXIT`) = game over: plays the `stream/GODS` movie, then returns to the title.

## Scripts — `macro/macroNNN`, `human/macroNNN` ✅ (core) / ✅ 60 game commands named / 🟡 rest
Decoded from the VM in `launchme` (dispatcher `0xa500`, context struct at `0x35460`). Disassembler: `tools/macro_dis.py`.

A script is a stream of big-endian `(tag, value)` int32 pairs ("tokens"):

| Tag | Meaning |
|---|---|
| 1 | integer literal |
| 2 | label = token index (jump target is `index × 2` words) |
| 3 | local variable (array at context `+0x10`) |
| 4 | opcode `0xFCxx`, comparator `0xFCD0–D5`, or `0` = separator |
| 5 | string character (file names) |
| 6 | global variable (game-wide array at `0x3D308`; flags, inventory, progress) |

The dispatcher executes opcode tokens and skips every other token, so assignments are infix: `<var> <op> <expr>`. Every opcode's token count comes from its handler's script-counter increment (`ARITY` in the tool). 133 opcodes in all.

**Execution model (from `0x6EB4` / `0xA500`):** every event — `FireEvent(a, b, c)` — restarts the script from the top. While not started only `DEF` and `BEGIN` execute, so the locals are re-initialised each time; then the `ON` lines act as a switch on the event and `WAIT` ends the run. Globals (`0x3D308`, 256) and 32 flags (`0x3D788`) persist. Events: `(1)` room entered, `(2)` every frame, `(t, 1, 0)` floor trigger, `(t, 2, 0)` examine, `(t, 3, item)` use item. `FOR v, a, b, s` … `NEXT` exits when `v + s == b`; `MARK` … `WHILE a cmp b` loops back while true; assignments write the token before the opcode; comparison codes `FCD0–D5` are `== > < != <= >=` (jump table base = instruction + 8). Previously: a block of `DEF var = n` runs at load (before `BEGIN`). Then the script registers event handlers: `ON (event) GOTO L` (`0xFC08`, one-value events such as 1 = room entered, 2 = every frame) and `ON (type, object, param) GOTO L` (`0xFC11`, e.g. `(41, 2, 0)`). It runs until `WAIT` (`0xFC02`) yields the frame. Call stack: 32 entries.

| Opcode | Name | Opcode | Name |
|---|---|---|---|
| FC01 | BEGIN (end of DEF block) | FC0D | FOR var, from, to, step |
| FC02 | WAIT (yield frame) | FC0E | NEXT |
| FC03 | GOTO label | FC0F | MARK (save resume point) |
| FC04 | PRINT (debug) | FC10 | WHILE |
| FC05 | IF a cmp b label (jumps when true) | FC11 | ON (a, b, c) GOTO: exact event match |
| FC06 | DEF var = const | FCC1/C2/C3 | FLAG_ON n / FLAG_OFF n / IF_FLAG n, L |
| FC08 | ON v GOTO: event's first value == v | FCD0–D5 | `==  >  <  !=  <=  >=` |
| FC09 / FC0A | CALL label / RETURN | FCE0–E7 | `= += -= *= /= %= ++ --` |

**Game commands.** The 60 most used commands (about 95% of game-command uses) are named in `GAME` in `tools/macro_dis.py`, from tracing each handler into its callee. Actors are handles into the 56-byte actor table at `0x58AA0`: 0 camera, 1 player body, 2 blend snapshot, 3–49 body poses, 50–59 close-up heads, 60 in-game head, 61+ hands/panels/items, 100+ room objects (assigned explicitly by the loader at `0x3480`).

| Offset | Actor field |
|---|---|
| +0 | visible |
| +4…+0xC | position (16.16) |
| +0x10…+0x18 | rotation (16.16, 256 per turn) |
| +0x2C | skip visibility cull |
| +0x30 | area |

| Group | Commands |
|---|---|
| Actors | `SET_POS`/`SET_ROT` actor,x,y,z · `ADD_POS`/`ADD_ROT` · `SHOW`/`HIDE` · `MOVE_FWD` actor,d · `LOOK_AT` a,b · `LOOK_AT_POINT` · `GET_X/Y/Z` · `ACTOR_AREA` · `ACTOR_FLAG` (= no-cull, +0x2C) · `NO_COLLIDE` · `IF_TOUCH` a,b,L |
| Scene | `AREA` n (current area, global `0x36698`) · `CAMERA` id (room camera table) · `CAMERA_RESET` (re-pick from the zone) · `DRAW` / `DRAW_B` / `DRAW_ROOM` (render a frame) · `CLEAR` · `FADE_OUT`/`FADE_IN` n · `PICTURE` n (full-screen still) · `SLEEP` vblanks |
| Player | `PLAYER_POS` x,y,z · `PLAYER_AREA` · `SHOW_PLAYER`/`HIDE_PLAYER` · `PLAYER_ANIM` slot · `PLAYER_ANIM_CUT` · `PLAYER_ANIM_STEP` · `DEF_PLAYER_ANIM` · `ATTACH_PARTS` · `HOLD` mesh |
| Objects | `OBJ_ANIM` · `OBJ_ANIM_STEP` · `DEF_OBJ_ANIM` (second animation table at `0x3C0E0`) |
| Text/sound | `MESSAGE` id,time,reset (queue of 32) · `SE` n · `SE_LOAD`/`SE_PLAY`/`SE_STOP` · `BGM` track |
| Flow | `IF_SKIP` L (Start/X) · `WAIT_RELEASE` · `IF_ITEM` item,L · `ITEM_SLOT` · `ITEM_SCREEN` · `LOAD_ROOM` n · `LOAD_SCRIPT` n · `EXIT` (end the room loop) · `SAVE?` |

**Areas and cameras are script-driven.** A cutscene switches to its own area (`AREA 9`, `ACTOR_AREA obj, 9`), poses the camera actor directly with `SET_POS`/`LOOK_AT`, draws frames itself in a `FOR` loop, then restores `AREA 0`. Room 001's chandelier cutscene (L357) is a clear example. In normal play the camera comes from the zone grid. `CAMERA_RESET` forces it to be re-picked.

**More animation slots:** player animation slots go past 16. Close-up face animations live in later slots (e.g. 24), defined at boot and by `DEF_PLAYER_ANIM`.

Unnamed so far: about 40 rare commands, plus `SE_76`, `FC60`, `FC7F`. Getters that write the preceding variable: FC63–65, FC7B–7C, FC8D–8F, FCB7–BC.

## Text — `mes/` 🟡
- `start.script`: ASCII room table — `ROOMnnn id x y z` (spawn position).
- `mes/*.scp`, `MESSAGE*.TXT`: Shift-JIS text with section markers (`玄関(F1R001)` = entrance hall).

## Audio and video 🟡
- `music/*.aifc`: AIFC, SDX2 2:1, 44.1 kHz stereo.
- `effect/`: AIFF/AIFC, `Geffect.list` index.
- `stream/*DS`: 3DO DataStream (32 KB blocks): `SHDR`, `CTRL`, `FILL`, `SNDS` (audio), `FILM` (`FHDR`/`FRME`, Cinepak `cvid`, 30 fps; 260×200, 300×160, 320×240…).

## Executable — `launchme` 🟡
ARM AIF, not compressed: RO 197,324 B, RW 46,740 B, ZI 280,292 B, image base 0. Debug/error strings are stored inline after the functions that use them. `tools/armanalyze.py` finds ~1,060 functions and resolves string references.
- Named (Norcroft embedded names): only the 3DO SDK libraries, i.e. DataStreaming (DataAcq, SAudio and Ctrl subscribers) and the Cinepak codec (`Decompresx`, `ExpandCodeBook`…). So the FMV uses the stock SDK stream format.
- Game code is unnamed. Known so far: script VM dispatcher `0xa500`, expression evaluator `0xad40`, script handlers `0x753c–0xa4f0`; room-file loaders reference `roomXXX.bin` at `0x6ac0` / `0x6da0`; script loaders `0x6b6c` / `0x6d80` / `0x6f94`.
