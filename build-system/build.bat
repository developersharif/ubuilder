@echo off
rem UBuilder Build Script for Windows

setlocal enabledelayedexpansion

set "PROJECT_ROOT=%~dp0.."
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "INSTALL_DIR=%PROJECT_ROOT%\dist"

echo UBuilder Build Script
echo ====================
echo Project root: %PROJECT_ROOT%
echo Build directory: %BUILD_DIR%
echo Install directory: %INSTALL_DIR%
echo.

rem Clean previous build
if exist "%BUILD_DIR%" (
    echo Cleaning previous build...
    rmdir /s /q "%BUILD_DIR%"
)

rem Create build directory
echo Creating build directory...
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

rem Configure with CMake
echo Configuring with CMake...
cmake -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
      "%PROJECT_ROOT%"

if !errorlevel! neq 0 (
    echo CMake configuration failed!
    exit /b 1
)

rem Build
echo Building UBuilder...
cmake --build . --config Release

if !errorlevel! neq 0 (
    echo Build failed!
    exit /b 1
)

rem Install
echo Installing UBuilder...
cmake --install .

if !errorlevel! neq 0 (
    echo Installation failed!
    exit /b 1
)

echo.
echo Build completed successfully!
echo UBuilder executable: %INSTALL_DIR%\bin\ubuilder.exe
echo.
echo To test the build:
echo   %INSTALL_DIR%\bin\ubuilder.exe --version
echo   %INSTALL_DIR%\bin\ubuilder.exe --help
