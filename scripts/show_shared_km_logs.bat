@echo off
setlocal

echo ==== receiver-admin.log ====
type ".run\receiver-admin.log" 2>nul
echo.
echo ==== sender-admin.log ====
type ".run\sender-admin.log" 2>nul
