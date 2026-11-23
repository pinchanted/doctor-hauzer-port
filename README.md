# Doctor Hauzer — HD Port (PS Vita + Android)

A native engine reimplementation of **Doctor Hauzer** (Riverhillsoft, 3DO, 1994, Japan-only). It targets PS Vita and Android (Retroid Pocket Flip 2), with a PC build for development.

The point of this port is **graphics**: native-resolution 3D, 60 fps, AI-upscaled textures, stills and FMV, widescreen, and a texture-replacement system. See [`docs/PLAN.md`](docs/PLAN.md).

## You need your own disc

No game data is in this repository. The tools extract and rebuild every asset from a dump of the original disc:

- `Doctor Hauzer (1994)(Panasonic)(JP)[!]` — catalogue FZ-SJ0701, single-track MODE1/2048 image.

## Tools

| Script | Purpose |
|---|---|
| `tools/opera_extract.py <image.iso> <outdir>` | Unpacks the 3DO Opera file system; writes `manifest.json` next to `outdir`. |
| `tools/img2png.py <srcdir> <dstdir>` | Converts 3DO IMAG (`.img`) 16-bit stills (linear or LRform) to PNG. |
| `tools/cel.py` | 3DO CEL decoder library (all bpp, packed/unpacked, PLUT, AMV shading). |
| `tools/room.py <roomNNN.bin>` | Room file parser: shell mesh, object records, texture bank. |
| `tools/render_room.py <roomNNN.bin> <out.png>` | Offline software render of a room for format validation (`--cam`, `--at`, `--group`). |
| `tools/export_textures.py <OrgData> <outdir>` | Exports every unique texture (rooms, player, items) to PNG plus `textures.json`. |
| `tools/make_testset.py <textures> <stills> <outdir>` | Picks a representative upscaler test set and a contact sheet. |

## Status

Phase 1 (formats) — stills, textures and room geometry decoded; see [`docs/FORMATS.md`](docs/FORMATS.md). Room logic, player model and macro bytecode are next.
