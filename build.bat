@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul
cd /d "%~dp0"

echo ==================================================
echo   Tone_Tab build
echo ==================================================
echo.

rem ---- 1. Locate Visual Studio / Build Tools -------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [error] Visual Studio Installer not found.
    echo Install Visual Studio Build Tools 2022 with the C++ workload.
    goto :fail
)

set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo [error] The C++ build tools are not installed.
    echo Open Visual Studio Installer, Modify, and select "Desktop development with C++".
    goto :fail
)
echo Visual Studio: %VSDIR%

rem ---- 2. Locate CMake -----------------------------------------------------
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
    where cmake >nul 2>nul
    if errorlevel 1 (
        set "CMAKE=%LOCALAPPDATA%\Microsoft\WinGet\Links\cmake.exe"
    ) else (
        set "CMAKE=cmake"
    )
)
if not exist "%CMAKE%" (
    if /i not "%CMAKE%"=="cmake" (
        echo [error] CMake not found.
        echo Install CMake from https://cmake.org/download/ or run: winget install Kitware.CMake
        goto :fail
    )
)
echo CMake: %CMAKE%

rem ---- 3. Check the Windows SDK --------------------------------------------
set "HAS_SDK="
set "KITS=%ProgramFiles(x86)%\Windows Kits\10\"
for /f "tokens=2,*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\Windows Kits\Installed Roots" /v KitsRoot10 2^>nul ^| findstr KitsRoot10') do set "KITS=%%b"
for /d %%v in ("%KITS%Lib\10.*") do (
    if exist "%%~v\um\x64\kernel32.lib" set "HAS_SDK=1"
)
if not defined HAS_SDK (
    echo [error] Windows SDK not found.
    echo In Visual Studio Installer, Modify, Individual components, install the Windows 11 SDK.
    goto :fail
)

rem ---- 4. Extract JUCE and NAM ---------------------------------------------
if not exist "third_party\JUCE\CMakeLists.txt" (
    echo Extracting JUCE ...
    tar -xf "third_party\JUCE-9.0.4.zip" -C "third_party"
    if errorlevel 1 (
        echo [error] Could not extract third_party\JUCE-9.0.4.zip
        goto :fail
    )
)

if not exist "third_party\nam\NAM\dsp.h" (
    echo Extracting NAM core ...
    tar -xf "third_party\NAM-core.zip" -C "third_party"
    if errorlevel 1 (
        echo [error] Could not extract third_party\NAM-core.zip
        goto :fail
    )
)

rem ---- 5. Clear a stale CMake cache ---------------------------------------
if exist "build\CMakeCache.txt" if not exist "build\cmake_install.cmake" (
    echo Clearing an incomplete configuration ...
    del /q "build\CMakeCache.txt"
    if exist "build\CMakeFiles" rmdir /s /q "build\CMakeFiles"
)

rem ---- 6. Configure and build ---------------------------------------------
echo.
echo [1/2] Configuring ...
"%CMAKE%" -S . -B build -A x64
if errorlevel 1 (
    if exist "build\CMakeCache.txt" del /q "build\CMakeCache.txt"
    if exist "build\CMakeFiles" rmdir /s /q "build\CMakeFiles"
    goto :fail
)

echo.
echo [2/2] Building Release ...
"%CMAKE%" --build build --config Release --parallel
if errorlevel 1 goto :fail

copy /y "build\Tone_Tab_artefacts\Release\Tone_Tab.exe" "Tone_Tab.exe" >nul

echo.
echo ==================================================
echo   Build succeeded: Tone_Tab.exe
echo ==================================================
pause
exit /b 0

:fail
echo.
echo Build failed. See the messages above.
pause
exit /b 1
