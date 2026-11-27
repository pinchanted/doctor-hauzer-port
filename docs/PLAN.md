# Doctor Hauzer — Port Plan (PS Vita + Android)

*Started 2026-11-23. Source: `Doctor Hauzer (1994)(Panasonic)(JP)[!]` (TOSEC verified, catalog FZ-SJ0701), single-track MODE1/2048 image, 250 MB.*
*Project folder: `C:\dev\Game Decomps\doctor-hauzer` (`disc\`, `tools\`, `extracted\`).*

## Project goal

**Push the graphics as far as they can go.** A port that only matches the original isn't worth doing; emulators already run it. Every engine decision below favours visual fidelity, and the graphics roadmap is the main measure of progress. Gameplay, puzzles and atmosphere stay faithful to the original.

## Graphics roadmap

Each item is a menu option where it changes the look, so the original look stays available. Per the menu rule, a platform only shows options it can actually run.

**Tier 1 — baseline (both platforms)**
- 3D rooms, player and items rendered at native screen resolution (960×544 Vita, 1920×1080 Flip 2) instead of 320×240.
- 60 fps rendering with interpolated animation (the original runs well below 30).
- Texture filtering with mipmaps (the 3DO had none) plus MSAA; no more shimmering or crawling edges.
- All 638 unique textures AI-upscaled on MELCHIOR to 4× masters. They are 256-colour quantized (32-entry palette × 8 brightness levels); the test showed the chosen model handles that well without a separate de-banding pass.
- Stills (title, examine views, documents, item cards) AI-upscaled to 4× masters (1280×960), then resized per screen: 1440×1080 Flip 2, 725×544 Vita.
- High-resolution font rendering (vector or 4× bitmap), not the 3DO's bitmap font.
- FMV upscaled plus frame-interpolated to 60 fps, H.264 hardware decode on both targets.

**Tier 2 — presentation**
- 16:9 widescreen for the 3D rooms. Fixed cameras can expose unmodelled geometry at the frame edges, so each camera gets checked, with a per-camera fallback (pillarbox or extended art).
- Stills and FMV in 4:3 inside a designed frame, or reframed where the art allows.
- English stills re-lettered at high resolution (not the 320×240 fan-patch images).
- Full 24-bit colour pipeline end to end.

**Tier 3 — enhancement (Android first; Vita where performance allows)**
- Per-pixel lighting and distance fog tuned to the original's mood; real-time shadow for the player.
- Smoothed normals / subdivision on the player and item models.
- **Texture-replacement system** (Dolphin-style: keyed by texture ID): drop hand-made or curated HD textures into a folder to override the AI ones. Best results on hero assets (doors, puzzles, documents) come from hand-tuning, not raw AI output.
- Optional post effects: subtle bloom on light sources, film grain, CRT-free sharpening.

**Uprez model:** textures and stills use **4x-PBRify_UpscalerDAT2_V1** (chosen 2026-11-23 from a 27-variant test; see `docs/UPREZ.md`). FMV gets its own test later.

## Disc survey (done)

The disc uses the 3DO Opera file system; `tools/opera_extract.py` unpacks it to `extracted/fs` (398 entries, 155 MB, file list in `extracted/manifest.json`). Game data lives under `OrgData/`:

| Folder | What it is | Format | Size |
|---|---|---|---|
| `launchme` | Game executable | ARM AIF, uncompressed; 197 KB code, 46 KB data, 280 KB zero-init. Big-endian ARM60. Debug/error strings left in | 246 KB |
| `macro/` (39) | **Game scripts**: per-room event logic plus demo/ending | Custom bytecode VM (GOTO/CALL/IF/FOR/WHILE/SET/FlagOn… per the exe's error strings) | 220 KB |
| `room/` (33) | Room data, one file per room (floors 0xx/2xx/3xx) | Custom binary: header of offsets, geometry and textures | 8 MB |
| `start.script` | Room table: id plus spawn x/y/z per room | ASCII | 1 KB |
| `human/` | Player model (`HumanPol.bin` polygons, `HumanTex.bin` textures) plus 6 macros | Custom binary | 280 KB |
| `item/` | Item models and textures | Custom binary | 70 KB |
| `mes/` | **All dialogue/notes text** (`mes.scp`, `MESSAGE*.TXT`, per-floor `.message`) | Plain Shift-JIS text | 100 KB |
| `font/fontNew.bin` | Kanji/kana font | Custom bitmap | 36 KB |
| `images/`, `isearch/`, `search/`, `window/`, `menu/` | Full-screen stills: title, examine views, documents, item cards, UI | 3DO IMAG 320×240 16-bit, LRform. **Decoder done** (`tools/img2png.py` → `extracted/png`) | ~12 MB |
| `etc/`, `window/*.cel` | Small sprites (logo, cursor, loading) | 3DO CEL | small |
| `menu/*.anim` | Animated menu items | 3DO ANIM (CEL frames) | 100 KB |
| `stream/` (8) | FMV: opening, ending, game over, events | 3DO DataStream: **Cinepak** video at 30 fps, about 260×200–300×160 (PADDS 320×240), with SDX2 audio | 94 MB |
| `music/` (12) | Background music | AIFC, SDX2 2:1, 44.1 kHz stereo | 45 MB |
| `effect/` (50) | Sound effects | AIFF/AIFC; `Geffect.list` index | 0.7 MB |
| `program/sramtools` | Save-game helper task | ARM AIF | 68 KB |

## Key findings

1. **The game logic is data.** Room events, puzzles and traps run as bytecode in the `macro` files, interpreted by a VM inside `launchme`. The port needs to reimplement the engine and the macro interpreter. It does not need to decompile game logic function by function. The engine's error strings name the opcodes, which gives a head start.
2. **The executable is small and unprotected.** ~197 KB of ARM code with descriptive error strings. A full decomp is feasible, but the plan is a native engine reimplementation (as with Sewer Shark), using the exe as reference only.
3. **Translation is easy for text, harder for images.** Dialogue and notes are plain Shift-JIS text files, so swapping in English is a data change. But many stills (item descriptions, documents, diary) have **Japanese text baked into the image**. They need English replacements: either the fan patch's images (diff the romhacking.net v1.2.1 patch against this dump) or new high-res lettering, which is better anyway after the uprez.
4. **Every asset type maps to an uprez path:**
   - 320×240 stills (title, examine views, documents) → AI upscale to 4× masters, resized per screen; re-letter text-bearing ones.
   - Room and model textures (3DO CELs) → AI upscale; 3D geometry re-rendered at native resolution and 60 fps.
   - Cinepak FMV (~260×200 @ 30 fps) → decode, AI upscale plus frame interpolation, re-encode H.264 (Vita hardware decoder, Android MediaCodec). Same pipeline as Sewer Shark.
   - SDX2 audio → decode to PCM → Opus/OGG.

## Architecture (both targets from day one)

```
doctor-hauzer/
  engine/        shared C core: macro VM, room/scene, actor + tank controls, camera, inventory, text, save
  render/        3DO-CEL-style quad renderer on OpenGL ES 2 / GXM via a thin abstraction
  platform/pc       SDL2 dev build (fast iteration, debugging)
  platform/vita     VitaSDK (SDL2 for Vita or vita2d/GXM), 960×544
  platform/android  NDK + SDL2, 1920×1080 (RP Flip 2)
  tools/         Python: extractors, converters, asset packer
  assets_src/    extracted originals (never shipped)
  assets_hd/     4× upscaled masters built on MELCHIOR; per-target packs derived from them
```

The original disc stays the source of truth. The tools rebuild the HD packs from it, so the repo needs no copyrighted data.

## Phases

1. **Formats** — decode `room*.bin`, `HumanPol/Tex`, `ItemPol/Tex`, CEL/ANIM, the font, and the macro bytecode. Open `launchme` in Ghidra (ARM, big-endian, base 0, AIF header stripped) and label the Portfolio OS calls first. Then find the room loader and the macro VM switch.
2. **Viewer** — PC SDL2 tool that loads a room and renders it with the player model. This is the proof that the formats are right.
3. **Macro VM** — interpreter plus flag/variable state; run room 001 end to end on PC.
4. **Full game on PC** — all rooms, inventory, examine screens, text, audio, FMV, save/load.
5. **Vita and Android front-ends** — input mapping, platform menus (only options each platform supports), suspend/resume.
6. **HD assets** — starts early, in parallel with phase 2: as soon as textures decode, begin upscaler tests so the asset pipeline is mature by the time the engine is. Tier 1 assets are a release requirement, not polish.
7. **Graphics tiers 2–3** — widescreen, lighting, texture-replacement system.
8. **QoL** — faster walk/turn, optional modern (analog) controls alongside tank controls, quick save.

## Status

- **2026-11-23** — Disc unpacked; stills, CEL textures, texture banks and meshes decoded (`docs/FORMATS.md`). Offline room renderer (`tools/render_room.py`) confirms room geometry, doors and windows. All 638 textures export cleanly. Upscaler test set (69 textures + 10 stills) is in `upscale_test/originals` on MELCHIOR.

- **2026-11-23** — Upscaler test run (9 models × 3 pre-passes, 1080p room renders). **Chosen: 4x-PBRify_UpscalerDAT2_V1, no pre-pass** (`docs/UPREZ.md`). Production upscale of all 638 textures and 77 stills: `tools/upscale_all.bat`.

- **2026-11-23** — Production upscale done on MELCHIOR: 638 textures + 77 stills at 4×, 0 failures, verified (sizes, transparency). Script VM decoded: token format, 133 opcodes with exact operand counts, control flow/arithmetic/events named; all 45 scripts disassemble (`tools/macro_dis.py`).

- **2026-11-23** — Room format fully mapped from the loader code: object records start at 0x18 (record 0 = shell) with position, rotation, texture table, trigger id, area and visibility; collision grid doubles as the camera-zone map; camera table found (3–14 fixed cameras per room). Rooms now render from the game's own cameras. Rooms hold extra "areas" (e.g. door-transition scenes).

- **2026-11-23** — Faces are variable length (triangles were being dropped): every mesh in the game now parses exactly. Player character decoded: 47 vertex-animated body poses with head and hands attached at per-frame marker triangles, close-up expression heads, items, YES/NO panels; texture tables read from the executable. The player renders correctly inside rooms from the game's cameras.

- **2026-11-24** — Player animation system decoded from the code: 17 animations, each a list of key poses with a per-animation speed and loop point. Poses blend linearly, and every animation change cross-fades from the current pose. Movement names confirmed by code (`docs/FORMATS.md`, `tools/anim.py`).

- **2026-11-24** — Player control decoded end to end. That covers the full button map, all 16 movement states (walk/run/turn speeds, jump, push/pull blocks, examine, use item), wall-sliding collision, and how examine and floor triggers raise the script's `ON` events. `tools/player.py` simulates it in real rooms. In room 001, walking into the door and pressing A raises exactly the event the script listens for. The original has no fixed frame rate (one tick per rendered frame), so the port gets a configurable logic rate.

- **2026-11-24** — 60 script game commands named from their handlers, covering about 95% of uses: actor position/rotation/visibility, area and camera switching, frame drawing, player and object animation, messages, sound and music, fades and stills, items, and room/script loading. The room scripts now read as plain cutscene and puzzle logic. Areas and cameras turn out to be script-driven in cutscenes and zone-driven in normal play.

- **2026-11-24** — Exact camera projection recovered from the code: 256 px focal length on 320×240, with the eye 64 units behind the camera position, giving 50.19° vertical FOV. The renderer now uses it, so shots frame exactly as in the original. Also decoded: room-record fields +0x64/+0x68/+0x6C (billboard flags, draw routine, no-cull). That closes the room format.

- **2026-11-24** — **First engine build** (`engine/`, C + SDL2, one GL path at GLES2 level for PC/Android/Vita). It loads any room from the extracted disc and draws it from the zone cameras at the game's exact projection, with the 4× HD textures (the original CELs remain as fallback, toggled with H). The player walks, runs, turns, jumps and pushes with the original speeds, collision and animations, interpolated at display rate. Examining a door raises the same event the room script listens for. A Windows build is cross-compiled, so no compiler is needed on MELCHIOR: `engine\run_hauzer.bat`.

- **2026-11-24** — **Room scripts run in the engine.** The VM is ported from the exe, and decoding it showed how the original works:
  - **Events:** every event (room entered, every frame, floor trigger, examine, use item) restarts the room script from the top, and the `ON` lines act as a switch on the event.
  - **Cutscenes:** cutscene loops draw their own frames, so the VM suspends on `DRAW` and resumes next frame.
  - **Disassembler fix:** the comparison table was shifted by one, now corrected; e.g. "skip the intro once it has played" now reads correctly.
  - **Room 001 end to end:** the intro cutscene (skippable with Start), the falling-chandelier trap on a 30-frame timer, locked-door messages, and the far door's opening cutscene leading into room 002 at the scripted position.
  - **All 29 rooms:** each loads and runs its script without errors.

- **2026-11-24** — Fixed what Aaron saw on the first build: the player stayed hidden after cutscenes, and the camera stayed on the cutscene view. The decoding and additions behind it:
  - **Per-frame camera and player code:** re-aim only on a zone change or after `CAMERA_RESET`; the body is shown every frame.
  - **The three view modes on L:** fixed cameras, overhead, first person.
  - **Rendering:** zone-change events, the object cull and one-sided walls (fixes the rooms that looked into walls), the script-camera angle convention, and close-up faces drawn over the scene as the 3DO did.
  - **Messages on screen:** Japanese for now, at full resolution with Noto Sans JP; an English file can be swapped in.
  - **Fades, death and game over:** the chandelier crushes you, then the game-over screen, then a new game.
  - **Sound effects, footsteps and music.**

- **2026-11-24** — **English.**
  - **What the fan translation changes:** compared with the original disc, the patch (ArcaneAria v1.0) changes the text and 54 stills; launchme is untouched.
  - **Text:** `tools/make_text_en.py` turns its all-caps text into sentence case for the engine's font, and adds English for the two messages the patch missed (ids 400/401, which live in the exe). The engine shows English by default (`--jp` for Japanese).
  - **Stills:** the patched stills (examine pages, controls, ending text) are stored as CELs, not IMAG. `img2png.py` now reads both, and `tools/upscale_english.bat` upscales them into `assets_hd/stills_en`.

- **2026-11-26** — **Items, menus and saves.**
  - **Decoded from the exe:** item models (item id = `ItemPol` mesh), names, idle spin, examine page lists, the inventory routines, the Start menu, item use, the pick-up prompt and `PICTURE` (`docs/FORMATS.md`, "Items, inventory and menus").
  - **In the engine (`ui.c`):** Start opens the item menu; the item turns in the spotlight, rendered at full resolution with HD textures. Use runs the room's handlers or says the key doesn't work. Examine shows the 4× upscaled pages (English set when playing in English) with the model beside them; the diaries turn pages. Scripts' item pick-ups (Yes/No), `IF_ITEM`, used-up items and full-screen pictures all work. The notebook (item 28) saves to 3 slots; F1–F3 load and `--load N` starts from a save.
  - **English lettering:** the 4× upscaled English pages read cleanly, so re-lettering them is optional polish. One gap in the fan patch: the vase's page (`sea22`) is still in Japanese.

- **2026-11-27** — **Title screen and movies.**
  - **Boot and title, as the original:** the opening movie, then the title movie with OPENING / START / CONTINUE / OPERATION drawn at full resolution with each item's coloured glow. START plays the two intro movies and begins with the notebook; CONTINUE opens the notebook saves; OPERATION plays the controls movie; game over plays its movie and returns to the title. Rooms 304/305 play their tunnel movie from the script.
  - **Movies in the engine:** the 3DO streams are rebuilt as MPEG-1 + MP2 and decoded with pl_mpeg (one portable path for PC, Android and Vita). `tools/movies_build.py` extracts the Cinepak frames (with the 3DO quirk bytes removed and EDDS's three frame sizes centred), upscales them 4x and encodes them. The frames are extracted on MELCHIOR and Lanczos previews are installed; `tools\movies_build.bat` runs the AI upscale.
  - **English movies:** the fan patch never translated the movies (only padding bytes changed). The engine now covers the baked-in Japanese and draws English captions at screen resolution: the opening's headlines and narration, the door scene's lines, the four narration pages before the game, and the controls labels.

- **2026-11-27** — **HD movies, the ending and the demos.**
  - All 7,423 movie frames AI-upscaled on MELCHIOR (4x-PBRify_RPLKSRd_V3) and encoded: `assets_hd/movies`, 210 MB.
  - **Ending:** room 313's `ENDING` command now plays `EDDS`, then the closing text pages (the fan patch's English) and the fly-through of eight rooms (rooms switch under the running script, as on the 3DO), then `End.img` and back to the title. Start skips the fly-through.
  - **Attract demos:** when the title movie runs out, the three demo scripts play (any button stops them), then the opening, then the title.
  - **`PICTURE` corrected:** it never waited for a button; the picture stays on screen while the script's own `WAIT_BUTTON` (decoded, previously ignored) waits, which removes a double button press at the room 001 windows.
- **2026-11-27** — **Mirror and door fixes** (from play-testing).
  - The whole view was mirrored left to right relative to the 3DO, which also made Left and Right turn the wrong way on screen. The projection is now flipped to match.
  - The door scenes showed the wall through opening doors because coplanar objects drew in the wrong order. Later objects now win, as on the 3DO.

## Next steps

1. Play-test the whole game on PC end to end, fixing what breaks (rooms 2xx/3xx puzzles, items, saves).
2. Vita and Android front ends (input mapping, per-platform menus, movie playback cost on Vita).
3. Re-letter the untranslated vase page (`sea22`); optional: re-letter all English pages at full resolution.
4. Face animations in cutscenes (player slots 17+), room 211's cutscene camera.
5. Optional: interpolate the 15 fps movies to 30/60 fps.
6. Calibrate the logic rate (the original has none) against gameplay footage, now that the PC build runs.
