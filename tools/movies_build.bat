@echo off
rem Doctor Hauzer: upscale the movie frames 4x on the GPU (tools\movies_build.py, stage "upscale").
rem The frames were extracted beforehand into work\movies; re-run to continue after a stop.
rem Options pass through, e.g.  movies_build.bat --model pbrify_dat2   (slower, sharper)
setlocal
set "PY=C:\dev\ai\ComfyUI_windows_portable\python_embeded\python.exe"
if not exist "%PY%" (
  echo Could not find %PY%
  pause
  exit /b 1
)
title Doctor Hauzer movie upscale
"%PY%" -u "%~dp0movies_build.py" upscale %*
echo.
echo Upscale finished. Tell Claude, or run "movies_build.py encode" where ffmpeg is installed.
pause
