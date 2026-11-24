@echo off
rem Doctor Hauzer: 4x upscale of the 54 stills the English fan patch re-lettered
rem (examine pages, controls screen, ending text). Output: assets_hd\stills_en. About 2-3 minutes.
setlocal
set "PY=C:\dev\ai\ComfyUI_windows_portable\python_embeded\python.exe"
if not exist "%PY%" (
  echo Could not find %PY%
  pause
  exit /b 1
)
title Doctor Hauzer - English stills upscale
"%PY%" -u "%~dp0upscale_all.py" --english
echo.
pause
