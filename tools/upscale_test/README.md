# Upscaling test

Double-click **`run_upscale_test.bat`**. It uses ComfyUI portable's Python (PyTorch, CUDA and spandrel are already there from the Sewer Shark setup) and needs no installs.

What it does, unattended (roughly 30–60 minutes on an RTX 3070):

1. Downloads 9 upscaling models and 2 clean-up models into `C:\dev\ai\upscale_models` (~1.5 GB, SHA-256 verified).
2. Prepares the test set in `upscale_test\originals`: repeating textures are wrap-padded (no seams), transparent ones get their colours spread into the transparent area (no dark fringes), stills are mirror-padded.
3. Runs every model three ways: as-is, after a de-banding pass, and after a de-dithering pass. That's 27 versions, plus original and Lanczos baselines.
4. Renders six rooms at 1920×1080 with each version's textures (bilinear filtering, 2× supersampling), so the models can be judged in context.
5. Opens `upscale_test\results\compare.html`: A/B slider, all-versions grid, and a ★ pick per image. **Copy summary** puts the tally on the clipboard to paste to Claude.

Re-running skips finished work, so it can be stopped and resumed. Options: `--only lollypop gtav_rgt_dither` (model keys), `--no-renders`, `--fp16` (faster, slightly lower quality), `--quick` (2-model smoke test).

Logs: `upscale_test\results\logs`. Model licences vary (several are CC BY-NC-SA: fine for a non-commercial fan port).
