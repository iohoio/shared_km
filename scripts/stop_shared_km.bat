@echo off
setlocal

taskkill /IM shared_km_sender.exe /F >nul 2>nul
taskkill /IM shared_km_receiver.exe /F >nul 2>nul

echo Stopped shared_km sender/receiver if they were running.
