@echo off
rem Builds taskbar-mediaplayer.exe with Visual Studio 2022 (C++ desktop workload) and CMake.
rem Output: build\Release\taskbar-mediaplayer.exe
setlocal
cd /d "%~dp0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || exit /b 1
cmake --build build --config Release || exit /b 1
echo Built build\Release\taskbar-mediaplayer.exe
