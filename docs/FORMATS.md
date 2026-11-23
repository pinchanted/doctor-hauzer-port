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
then  n_faces × (i32 n, i32 tex, i32 v0, v1, v2, v3)   n = 4 (quads)
```
Quad → texture mapping: v0 = top-left, v1 = top-right, v2 = bottom-right, v3 = bottom-left (3DO CEL corner mapping; verified by render).

## Room files — `room/roomNNN.bin` 🟡
| Offset | Meaning |
|---|---|
| 0x00 | texture bank offset ✅ |
| 0x04 | section B offset ❓ (~12 KB in room001; follows the bank) |
| 0x08 / 0x0C | section C offset / size ❓ (3.2 KB) — candidates: cameras, collision, zones |
| 0x10 | section E offset ❓ (to EOF, 2.4 KB) |
| 0x14 | count (10 in room001) 🟡 |
| 0x18 | object id base (100) ✅ |
| 0x1C | room shell mesh offset ✅ — shell faces use bank indices directly |
| 0x20 | ❓ |
| 0x3C | count + list of ints ❓ (resembles an object texture table) |
| 0x80… | object records, 0x80 bytes each, up to the shell mesh |

Object record:
| Offset | Meaning |
|---|---|
| 0x00–0x0C | flags/state ❓ (e.g. `1,2,1,36` — possibly event or macro links) |
| 0x10 | **state group** 🟡 — 0 = initial; 1, 2 … alternate versions (e.g. door slab open/closed) swapped by events |
| 0x14 | 1 |
| 0x18 | object id ✅ |
| 0x1C / 0x20 | mesh offset / size ✅ |
| 0x24–0x2C | position x, y, z ✅ |
| 0x40… | **texture table**: face tex `t` → bank index `word[16 + t − 1]` ✅ |

Open questions: what selects the visible state group; room001 object 105 (a large partition-like mesh in group 0); sections B/C/E. These need the room loader in `launchme`.

## Player and items 🟡
`human/HumanPol.bin`: offset table, then meshes in the standard mesh format (first: 91 verts, 96 faces) — likely body parts and/or animation frames. `human/RGB.bin` starts with `mato` (material?) ❓. `item/ItemPol.bin` similar.

## Scripts and text 🟡
- `start.script`: ASCII room table — `ROOMnnn id x y z` (spawn position).
- `macro/macroNNN`: bytecode for the room event VM. Opcodes per exe strings: GOTO, CALL, RETURN, IF, FOR, NEXT, WHILE, SET, ADD, INC, SUB, DEC, MUL, DIV, FlagOn, FlagOff…
- `mes/*.scp`, `MESSAGE*.TXT`: Shift-JIS text with section markers (`玄関(F1R001)` = entrance hall).

## Audio and video 🟡
- `music/*.aifc`: AIFC, SDX2 2:1, 44.1 kHz stereo.
- `effect/`: AIFF/AIFC, `Geffect.list` index.
- `stream/*DS`: 3DO DataStream (32 KB blocks): `SHDR`, `CTRL`, `FILL`, `SNDS` (audio), `FILM` (`FHDR`/`FRME`, Cinepak `cvid`, 30 fps; 260×200, 300×160, 320×240…).

## Executable — `launchme` 🟡
ARM AIF, not compressed: RO 197,324 B, RW 46,740 B, ZI 280,292 B. Debug/error strings intact (file paths, macro opcode names).
