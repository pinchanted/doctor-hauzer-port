# Doctor Hauzer engine (C, SDL2)

Native reimplementation of the 3DO game's engine, built from the formats and code paths documented in
`../docs/FORMATS.md`. One shared core for PC, PS Vita and Android; only the front end differs.

| File | What it is |
|---|---|
| `src/util.c` | file loading, big-endian readers, SHA-1 (HD texture keys) |
| `src/cel.c` | 3DO CEL decoder (port of `tools/cel.py`) |
| `src/room.c` | room files: objects, texture bank, zone/trigger grid, cameras |
| `src/human.c` | player model, marker-attached head/hands, the 17 animations and their stepper |
| `src/player.c` | input → 16 movement states, wall-sliding collision, push/pull, examine/floor triggers |
| `src/render.c` | GL renderer at GLES2 level (desktop GL 2.1 / GLES 2+ / vitaGL); the game's exact projection; HD texture override by hash |
| `src/script.c` | room script VM (event model of `0x6EB4` / `0xA500`) and the game commands |
| `src/text.c` | messages (`mes.scp` Shift-JIS + exe table for ids 400+, optional UTF-8 override) rendered with stb_truetype |
| `src/game.c` | room loading, spawn table, zone cameras, fixed-rate logic with display-rate interpolation |
| `src/main_pc.c` | SDL2 front end + headless screenshot harness |

## Run (Windows)
`run_hauzer.bat` - expects `..\extracted\fs` (the unpacked disc) and `..\assets_hd\textures`.

Keys: arrows = d-pad · Z = A (examine) · X / Shift = B (run, push/pull) · C / Space = C (jump) ·
Enter = Start (item menu; skips cutscenes) · **Q = L: change view (fixed cameras / overhead / first person)** · W = R · **PageUp / PageDown = previous / next room** · **H = HD / original
textures** · F1–F3 = load save 1–3 (Shift+F1–F3 = save) · F12 = screenshot · Esc = quit. A game controller works too (A examine, X run, B jump).

**Title:** without `--room` the game boots like the original: the opening movie (Start skips), then the title with OPENING / START / CONTINUE / OPERATION. Movies come from `assets_hd\movies` (built by `tools\movies_build.py`); English captions for their Japanese text come from `text\movies_en.txt`. `run_hauzer.bat --room 1` skips straight into a room.

**Item menu (Start):** Left/Right choose an item, Up/Down choose Use / Examine, A confirms, X or B closes. On an examine page the d-pad spins the item, A turns the page. Use the **Notebook** to save (3 slots, stored in `%APPDATA%\Pinchanted\DoctorHauzer`). Stills come from `assets_hd\stills` (and `stills_en` in English) next to the `--hd` folder, or `--stills <dir>`; without them the original pictures are read from the disc.

Options: `--text file.txt` (UTF-8, one message per line; default `text/mes_en.txt` beside the exe, made by `tools/make_text_en.py` from the English fan-translation disc), `--jp` (original Japanese text), `--room N`, `--hz N` (logic rate, default 20 - the original had no fixed rate),
`--size WxH`, `--camera N`.

## Build
- Linux: `cmake -B build && cmake --build build` (needs SDL2 dev).
- Windows from Linux: `./build_windows.sh <SDL2-2.30.9 mingw package> <out>`.
- Headless test: `SDL_VIDEODRIVER=offscreen ./hauzer --data … --shot out.png --input "UP*30,A"`.

## Not yet
FMV (title, game-over and ending movies), items and the Start menu, examine pictures, face animations in cutscenes.

## Fonts
`./fetch_assets.sh` downloads Noto Sans JP (SIL OFL 1.1) into `assets/fonts/`; the Windows build ships it in `fonts/`.
