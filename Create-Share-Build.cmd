@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\New-ShareBuild.ps1" -ProjectRoot "%~dp0."
if errorlevel 1 (
  echo Export failed. Your personal dist is unchanged.
  pause
  exit /b 1
)
echo Ready: dist-share
pause
