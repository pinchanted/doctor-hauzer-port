@echo off
rem Doctor Hauzer production upscale: every texture and still with 4x-PBRify_UpscalerDAT2_V1.
rem Double-click to run; re-run to continue after a stop.
setlocal
set "PY=C:\dev\ai\ComfyUI_windows_portable\python_embeded\python.exe"
if not exist "%PY%" (
  echo Could not find %PY%
  pause
  exit /b 1
)
title Doctor Hauzer production upscale
"%PY%" -u "%~dp0upscale_all.py" %*
set "HTML=%~dp0..\assets_hd\review\compare.html"
if exist "%HTML%" start "" "%HTML%"
echo.
pause
