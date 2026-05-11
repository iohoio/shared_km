@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0move_mouse_to_click_target.ps1"
exit /b %errorlevel%
