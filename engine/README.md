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
| `src/game.c` | room loading, spawn table, zone cameras, fixed-rate logic with display-rate interpolation |
| `src/main_pc.c` | SDL2 front end + headless screenshot harness |

## Run (Windows)
`run_hauzer.bat` - expects `..\extracted\fs` (the unpacked disc) and `..\assets_hd\textures`.

Keys: arrows = d-pad · Z = A (examine) · X / Shift = B (run, push/pull) · C / Space = C (jump) ·
Enter = Start · Q / W = L / R · **PageUp / PageDown = previous / next room** · **H = HD / original
textures** · F12 = screenshot · Esc = quit. A game controller works too (A examine, X run, B jump).

Options: `--room N`, `--hz N` (logic rate, default 20 - the original had no fixed rate),
`--size WxH`, `--camera N`.

## Build
- Linux: `cmake -B build && cmake --build build` (needs SDL2 dev).
- Windows from Linux: `./build_windows.sh <SDL2-2.30.9 mingw package> <out>`.
- Headless test: `SDL_VIDEODRIVER=offscreen ./hauzer --data … --shot out.png --input "UP*30,A"`.

## Not yet
Room scripts (the VM), doors/room changes, messages and text, menus and items, sound, music, FMV.
