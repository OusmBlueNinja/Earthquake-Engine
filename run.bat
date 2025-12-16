@echo off
cmake -S . -B build || exit /b 1
cmake --build build --config Debug || exit /b 1
.\build\bin\app.exe || exit /b 1
