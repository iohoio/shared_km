@echo off
setlocal

if not exist ".run" mkdir ".run"

del /Q ".run\sender-live.log" 2>nul
del /Q ".run\sender-real.log" 2>nul
del /Q ".run\receiver-admin.log" 2>nul
del /Q ".run\receiver-admin.err.log" 2>nul
del /Q ".run\receiver-integration.log" 2>nul

echo Cleared shared_km log files.
