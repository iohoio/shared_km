@echo off
setlocal

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%

if not exist ".run" mkdir ".run"

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
 "Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','set SHAREDKM_DRY_RUN_INPUT=1 && \"%CD%\build-vs3\apps\Debug\shared_km_receiver.exe\" > \"%CD%\.run\receiver-dry-run.log\" 2>&1' -WorkingDirectory '%CD%' -Verb RunAs"
