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

## Status

Phase 1 (formats) — disc survey done; room, model and macro bytecode formats are next.
