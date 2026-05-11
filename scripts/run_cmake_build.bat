@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%

"C:\Program Files\CMake\bin\cmake.exe" -S . -B build -G Ninja
if errorlevel 1 exit /b %errorlevel%

"C:\Program Files\CMake\bin\cmake.exe" --build build
if errorlevel 1 exit /b %errorlevel%

"C:\Program Files\CMake\bin\ctest.exe" --test-dir build --output-on-failure
