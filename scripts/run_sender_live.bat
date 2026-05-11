@echo off
setlocal

if not exist ".run" mkdir ".run"

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%

echo Starting shared_km_sender...
echo Log: %CD%\.run\sender-live.log

"%CD%\build-vs3\apps\Debug\shared_km_sender.exe" > "%CD%\.run\sender-live.log" 2>&1
