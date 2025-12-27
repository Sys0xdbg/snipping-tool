@echo off
setlocal

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 (
    echo CMake configuration failed
    exit /b 1
)

cmake --build . --config Release
if %errorlevel% neq 0 (
    echo Build failed
    exit /b 1
)

echo.
echo Build successful! Executable: build\Release\snip.exe
echo.
echo Run snip.exe to launch the snipping tool GUI.
