@echo off
set "PATH="
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%

"C:\Program Files\CMake\bin\cmake.exe" -S . -B build-vs3 -G "Visual Studio 17 2022" -A x64 ^
 -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe"
if errorlevel 1 exit /b %errorlevel%

"C:\Program Files\CMake\bin\cmake.exe" --build build-vs3 --config Debug
if errorlevel 1 exit /b %errorlevel%

"C:\Program Files\CMake\bin\ctest.exe" --test-dir build-vs3 -C Debug --output-on-failure
