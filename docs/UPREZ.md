# Upscaling decisions

## Textures and stills — 2026-11-23

**Chosen: `4x-PBRify_UpscalerDAT2_V1` (Kim2091, DAT2), no pre-pass.**

Picked by Aaron after reviewing the upscale test (`tools/upscale_test`): 9 models × 3 pre-passes (none / Debandurh deband / DitherDeleterV3 dedither), judged on 69 textures, 10 stills and six rooms rendered at 1080p. It gave the best balance of added detail without smearing.

- Model: https://github.com/Kim2091/Kim2091-Models/releases/tag/4x-PBRify_UpscalerDAT2_V1, SHA-256 `ff133c35707986e253a9e84aae16ef0295847de41239f1eda53fbbeb0e2243b7`.
- Prep (shared with the test): 16 px source padding cropped after; wrap padding for opaque textures (no seams between neighbouring quads), colour bleed plus edge padding for transparent textures (no dark fringes; alpha upscaled with Lanczos), mirror padding for stills. Textures under 8 px are Lanczos-scaled.
- Full precision (fp32); 192 px tiles with feathered overlap for stills.
- Masters: 4×, lossless PNG, in `assets_hd/textures/<hash>.png` and `assets_hd/stills/<folder>/<name>.png`, made by `tools/upscale_all.bat`.

## Still to decide
- Per-platform packs: Android ASTC at 4×; Vita DXT at 4× if each room's set fits, otherwise 2× (decide once the engine loads rooms).
- Stills containing Japanese text: the upscaled versions are only backgrounds; English text gets re-lettered at full resolution.
- FMV: separate test (video models + frame interpolation), as with Sewer Shark.
