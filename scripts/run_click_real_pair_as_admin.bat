@echo off
setlocal

call "%~dp0clear_shared_km_logs.bat"
call "%~dp0stop_shared_km.bat"

echo Starting elevated receiver...
call "%~dp0run_receiver_as_admin.bat"

timeout /t 2 /nobreak >nul

echo Starting elevated sender...
call "%~dp0run_sender_test_as_admin.bat"

echo.
echo Real click integration launch requested.
echo Receiver log: %CD%\.run\receiver-admin.log
echo Sender log:   %CD%\.run\sender-admin.log
