@echo off
setlocal enabledelayedexpansion

echo ============================================================
echo   ACU Custom Client - Automated Build Script
echo   Target: Windows x64 Release (MSVC 2022)
echo ============================================================
echo.

:: Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake not found in PATH. Install CMake 3.25+ and add to PATH.
    exit /b 1
)

:: Check for Visual Studio
set "VS_FOUND=0"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022" set "VS_FOUND=1"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022" set "VS_FOUND=1"
if "%VS_FOUND%"=="0" (
    echo [WARNING] Visual Studio 2022 not detected in default location.
    echo           Build may fail if MSVC toolchain is not available.
    echo.
)

:: Configure
echo [1/3] Configuring CMake (x64 Release)...
echo.
cmake --preset x64-release
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] CMake configuration failed.
    exit /b 1
)
echo.

:: Build
echo [2/3] Building solution...
echo.
cmake --build build --config Release --parallel
if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed.
    exit /b 1
)
echo.

:: Verify outputs
echo [3/3] Verifying outputs...
echo.

set "BIN_DIR=%~dp0bin"
set "ALL_GOOD=1"

if exist "%BIN_DIR%\ACU-Launcher.exe" (
    echo   [OK] ACU-Launcher.exe
) else (
    echo   [MISSING] ACU-Launcher.exe
    set "ALL_GOOD=0"
)

if exist "%BIN_DIR%\ACU-Core.dll" (
    echo   [OK] ACU-Core.dll
) else (
    echo   [MISSING] ACU-Core.dll
    set "ALL_GOOD=0"
)

if exist "%BIN_DIR%\uplay_r1_loader64.dll" (
    echo   [OK] uplay_r1_loader64.dll
) else (
    echo   [MISSING] uplay_r1_loader64.dll
    set "ALL_GOOD=0"
)

echo.
if "%ALL_GOOD%"=="1" (
    echo ============================================================
    echo   BUILD SUCCESSFUL
    echo   Output: %BIN_DIR%\
    echo ============================================================
) else (
    echo ============================================================
    echo   BUILD COMPLETED WITH WARNINGS - Some outputs missing
    echo   Check the build log above for errors.
    echo ============================================================
)

endlocal
