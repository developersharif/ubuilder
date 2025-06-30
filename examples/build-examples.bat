@echo off
rem UBuilder Examples Build and Test Script for Windows
rem Builds UBuilder core, creates executables from all examples, and runs them

setlocal enabledelayedexpansion

rem Project paths
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "EXAMPLES_DIR=%PROJECT_ROOT%\examples"
set "OUTPUT_DIR=%EXAMPLES_DIR%\output"

rem Colors for output - Windows doesn't support ANSI colors by default
rem But we can use echo messages with prefixes

echo.
echo ==========================================
echo  UBuilder Examples Build and Test
echo ==========================================
echo.

rem Create output directory
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

rem Step 1: Build UBuilder core
echo ==========================================
echo  Building UBuilder Core
echo ==========================================
echo.

if exist "%BUILD_DIR%" (
    echo [INFO] Cleaning previous build...
    rmdir /s /q "%BUILD_DIR%"
)

echo [INFO] Creating build directory...
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo [INFO] Configuring with CMake...
cmake -DCMAKE_BUILD_TYPE=Release "%PROJECT_ROOT%"
if !errorlevel! neq 0 (
    echo [ERROR] CMake configuration failed
    exit /b 1
)

echo [INFO] Building UBuilder...
cmake --build . --config Release
if !errorlevel! neq 0 (
    echo [ERROR] Build failed
    exit /b 1
)

if not exist "%BUILD_DIR%\src\Release\ubuilder.exe" (
    if not exist "%BUILD_DIR%\src\ubuilder.exe" (
        echo [ERROR] UBuilder executable not found after build
        exit /b 1
    )
)

echo [SUCCESS] UBuilder core built successfully

rem Step 2: Find and build all examples
echo.
echo ==========================================
echo  Building All Examples
echo ==========================================
echo.

set "UBUILDER_EXE=%BUILD_DIR%\src\Release\ubuilder.exe"
if not exist "%UBUILDER_EXE%" (
    set "UBUILDER_EXE=%BUILD_DIR%\src\ubuilder.exe"
)

set BUILT_COUNT=0
set SKIPPED_COUNT=0
set FAILED_COUNT=0

rem Check for runtimes and build examples
for /d %%D in ("%EXAMPLES_DIR%\*") do (
    if exist "%%D\ubuilder.json" (
        call :build_example "%%D" "%%~nxD"
    )
)

rem Step 3: Run all successfully built examples
echo.
echo ==========================================
echo  Running All Built Examples
echo ==========================================
echo.

set RUN_SUCCESS_COUNT=0
set RUN_FAILED_COUNT=0

for %%F in ("%OUTPUT_DIR%\*.exe") do (
    call :run_example "%%F"
)

rem Final summary
echo.
echo ==========================================
echo  Build and Test Summary
echo ==========================================
echo.

echo Total examples built: %BUILT_COUNT%
echo Skipped (missing runtime): %SKIPPED_COUNT%
echo Build failures: %FAILED_COUNT%
echo Successfully ran: %RUN_SUCCESS_COUNT%
echo Runtime failures: %RUN_FAILED_COUNT%

if %RUN_SUCCESS_COUNT% gtr 0 (
    echo.
    echo Successfully built executables:
    for %%F in ("%OUTPUT_DIR%\*.exe") do (
        echo   * %%F
    )
)

if %RUN_SUCCESS_COUNT% equ %BUILT_COUNT% if %BUILT_COUNT% gtr 0 (
    echo.
    echo 🎉 ALL BUILDS AND TESTS SUCCESSFUL! 🎉
    echo All %BUILT_COUNT% example(s) built and ran successfully!
    exit /b 0
) else (
    echo.
    echo ❌ Some examples failed to build or run properly
    exit /b 1
)

goto :eof

:build_example
set "EXAMPLE_DIR=%~1"
set "EXAMPLE_NAME=%~2"
set "OUTPUT_EXECUTABLE=%OUTPUT_DIR%\%EXAMPLE_NAME%.exe"

echo [INFO] Building example: %EXAMPLE_NAME%

rem Read runtime from ubuilder.json (simplified parsing)
set "RUNTIME="
for /f "tokens=2 delims=:," %%A in ('type "%EXAMPLE_DIR%\ubuilder.json" ^| findstr "runtime"') do (
    set "RUNTIME=%%A"
    set "RUNTIME=!RUNTIME:"=!"
    set "RUNTIME=!RUNTIME: =!"
)

rem Check if runtime is available
set "RUNTIME_OK=0"
if /i "%RUNTIME%"=="python" (
    python --version >nul 2>&1 && set "RUNTIME_OK=1"
    if !RUNTIME_OK! equ 0 (
        python3 --version >nul 2>&1 && set "RUNTIME_OK=1"
    )
)
if /i "%RUNTIME%"=="php" (
    php --version >nul 2>&1 && set "RUNTIME_OK=1"
)
if /i "%RUNTIME%"=="node" (
    node --version >nul 2>&1 && set "RUNTIME_OK=1"
)

if !RUNTIME_OK! equ 0 (
    echo [WARNING] Skipping %EXAMPLE_NAME% due to missing %RUNTIME% runtime
    set /a SKIPPED_COUNT+=1
    goto :eof
)

echo [SUCCESS] %RUNTIME% runtime available

rem Build the example
echo [INFO] Creating executable for %EXAMPLE_NAME% (runtime: %RUNTIME%)...
"%UBUILDER_EXE%" --project-dir="%EXAMPLE_DIR%" --runtime=%RUNTIME% --output="%OUTPUT_EXECUTABLE%"
if !errorlevel! neq 0 (
    echo [ERROR] Failed to build %EXAMPLE_NAME%
    set /a FAILED_COUNT+=1
    goto :eof
)

if not exist "%OUTPUT_EXECUTABLE%" (
    echo [ERROR] Executable not created: %OUTPUT_EXECUTABLE%
    set /a FAILED_COUNT+=1
    goto :eof
)

echo [SUCCESS] Built %EXAMPLE_NAME% -^> %OUTPUT_EXECUTABLE%
set /a BUILT_COUNT+=1
goto :eof

:run_example
set "EXECUTABLE=%~1"
set "EXAMPLE_NAME=%~nx1"

echo [INFO] Running %EXAMPLE_NAME%...

if not exist "%EXECUTABLE%" (
    echo [ERROR] Executable not found: %EXECUTABLE%
    set /a RUN_FAILED_COUNT+=1
    goto :eof
)

echo --- Output from %EXAMPLE_NAME% ---
"%EXECUTABLE%"
if !errorlevel! neq 0 (
    echo --- End output ---
    echo [ERROR] Failed to run %EXAMPLE_NAME%
    set /a RUN_FAILED_COUNT+=1
    goto :eof
)
echo --- End output ---

echo [SUCCESS] Successfully ran %EXAMPLE_NAME%
set /a RUN_SUCCESS_COUNT+=1
goto :eof
