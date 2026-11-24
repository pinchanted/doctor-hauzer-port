@echo off
rem Doctor Hauzer engine - first PC build. Expects this folder next to extracted\ and assets_hd\.
cd /d "%~dp0"
hauzer.exe --data "..\extracted\fs" --hd "..\assets_hd\textures" --room 1 %*
if errorlevel 1 pause
