#!/bin/sh
# Cross-compile the Windows build on Linux (mingw-w64 + the SDL2 MinGW dev package).
#   sudo apt-get install mingw-w64
#   curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz
#   tar xzf SDL2-devel-2.30.9-mingw.tar.gz
# usage: ./build_windows.sh <path to SDL2-2.30.9> <out dir>
set -e
S="$1/x86_64-w64-mingw32"; OUT="${2:-dist-win}"
mkdir -p "$OUT"
x86_64-w64-mingw32-gcc -O2 -std=c11 -Wall -Wno-unused-parameter -o "$OUT/hauzer.exe" \
  src/util.c src/cel.c src/room.c src/human.c src/player.c src/render.c src/game.c src/main_pc.c \
  -I"$S/include/SDL2" -L"$S/lib" -lmingw32 -lSDL2main -lSDL2 -lopengl32 -static-libgcc
cp "$S/bin/SDL2.dll" "$OUT/"
cp run_hauzer.bat "$OUT/"
echo "built $OUT/hauzer.exe"
