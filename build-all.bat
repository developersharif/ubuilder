@echo off
rem UBuilder Build All Script for Windows
rem Universal wrapper script to build and test all UBuilder projects

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

echo.
echo 🚀 UBuilder Build All Projects
echo ===============================
echo Platform: Windows
echo.

echo For Windows, please use one of these commands:
echo.
echo For Command Prompt:
echo   examples\build-examples.bat
echo.
echo For PowerShell:
echo   examples\build-examples.ps1
echo.
echo For WSL/MSYS2/Git Bash:
echo   ./examples/build-examples.sh
echo.

rem Try to run the batch script
if exist "%SCRIPT_DIR%examples\build-examples.bat" (
    echo Running Windows batch script...
    echo.
    call "%SCRIPT_DIR%examples\build-examples.bat"
    if !errorlevel! equ 0 (
        echo.
        echo ✅ All UBuilder projects built and tested successfully!
        echo    Check the examples\output\ directory for generated executables.
        exit /b 0
    ) else (
        echo.
        echo ❌ Some builds or tests failed. Check the output above for details.
        exit /b 1
    )
) else (
    echo Error: build-examples.bat not found!
    exit /b 1
)
