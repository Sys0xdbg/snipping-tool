@echo off
REM Build script using MSVC command line compiler (cl.exe)
REM Run from Visual Studio Developer Command Prompt

if not exist bin mkdir bin

cl.exe /EHsc /std:c++17 /O2 /DUNICODE /D_UNICODE ^
    src\main.cpp ^
    /Fe:bin\snip.exe ^
    /link /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib windowscodecs.lib ole32.lib comdlg32.lib msimg32.lib user32.lib gdi32.lib

if %errorlevel% equ 0 (
    echo.
    echo Build successful! Executable: bin\snip.exe
) else (
    echo Build failed!
)
