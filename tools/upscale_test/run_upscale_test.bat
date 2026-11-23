@echo off
rem Doctor Hauzer upscaling test. Double-click to run; re-run to continue after a stop.
rem Uses the Python, PyTorch and CUDA from ComfyUI portable (set up for the Sewer Shark tests).
setlocal
set "PY=C:\dev\ai\ComfyUI_windows_portable\python_embeded\python.exe"
if not exist "%PY%" (
  echo Could not find %PY%
  echo This script uses ComfyUI portable's Python. See the Sewer Shark SETUP.md, step 1.3.
  pause
  exit /b 1
)
title Doctor Hauzer upscaling test
"%PY%" -u "%~dp0run_upscale_test.py" %*
set "HTML=%~dp0..\..\upscale_test\results\compare.html"
if exist "%HTML%" start "" "%HTML%"
echo.
pause
