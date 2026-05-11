@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_move_mouse.ps1"
exit /b %errorlevel%
