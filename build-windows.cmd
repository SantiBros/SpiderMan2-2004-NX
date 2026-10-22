@echo off
rem build-windows.cmd: double-click it, or drop your game onto it, to build Spider-Man 2 (Xbox) for Switch from
rem your own copy of the game on Windows 10 or 11. It sets up WSL and the tools the first time. The steps are in
rem tools\windows\build-windows.ps1.
title Spider-Man 2 (Xbox) for Switch - builder
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0tools\windows\build-windows.ps1" %*
echo.
pause
