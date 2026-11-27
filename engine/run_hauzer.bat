@echo off
rem Doctor Hauzer engine. Starts at the opening movie and title; add --room N to jump into a room. Expects this folder next to extracted\ and assets_hd\.
cd /d "%~dp0"
hauzer.exe --data "..\extracted\fs" --hd "..\assets_hd\textures" %*
if errorlevel 1 pause
