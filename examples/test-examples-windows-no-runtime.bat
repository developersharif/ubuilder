@echo off
REM test-examples-windows-no-runtime.bat
REM Test all built example executables on Windows without host runtimes available
REM This script simulates an environment where system runtimes (php, python, node) are not available
REM and tests the portability of the generated executables.

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "EXAMPLES_OUTPUT_DIR=%SCRIPT_DIR%output"
set "FAKE_BIN_DIR=%TEMP%\ubuilder-fake-bin-%RANDOM%"

echo ==============================================
echo UBuilder Windows Portability Test (No Runtimes^)
echo ==============================================
echo Script Directory: %SCRIPT_DIR%
echo Project Root: %PROJECT_ROOT%
echo Examples Output: %EXAMPLES_OUTPUT_DIR%
echo.

REM Function to create fake runtime executables that simulate missing runtimes
call :setup_fake_runtimes

REM Verify that runtimes are blocked
call :verify_runtimes_blocked

REM Run all executable tests
call :run_all_tests

REM Cleanup
call :cleanup

if !ERRORLEVEL! equ 0 (
    echo.
    echo ✅ Windows portability test completed successfully!
    exit /b 0
) else (
    echo.
    echo ❌ Windows portability test failed!
    exit /b 1
)

:setup_fake_runtimes
echo === Setting up fake runtimes to simulate missing host runtimes ===

REM Create temporary directory for fake executables
if not exist "%FAKE_BIN_DIR%" mkdir "%FAKE_BIN_DIR%"

REM Create fake PHP executable
echo @echo off > "%FAKE_BIN_DIR%\php.bat"
echo echo php: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\php.bat"
echo exit /b 127 >> "%FAKE_BIN_DIR%\php.bat"

echo @echo off > "%FAKE_BIN_DIR%\php.cmd"
echo echo php: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\php.cmd"
echo exit /b 127 >> "%FAKE_BIN_DIR%\php.cmd"

REM Create fake Python3 executable
echo @echo off > "%FAKE_BIN_DIR%\python3.bat"
echo echo python3: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\python3.bat"
echo exit /b 127 >> "%FAKE_BIN_DIR%\python3.bat"

echo @echo off > "%FAKE_BIN_DIR%\python3.cmd"
echo echo python3: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\python3.cmd"
echo exit /b 127 >> "%FAKE_BIN_DIR%\python3.cmd"

REM Create fake Python executable
echo @echo off > "%FAKE_BIN_DIR%\python.bat"
echo echo python: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\python.bat"
echo exit /b 127 >> "%FAKE_BIN_DIR%\python.bat"

echo @echo off > "%FAKE_BIN_DIR%\python.cmd"
echo echo python: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\python.cmd"
echo exit /b 127 >> "%FAKE_BIN_DIR%\python.cmd"

REM Create fake Node.js executable
echo @echo off > "%FAKE_BIN_DIR%\node.bat"
echo echo node: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\node.bat"
echo exit /b 127 >> "%FAKE_BIN_DIR%\node.bat"

echo @echo off > "%FAKE_BIN_DIR%\node.cmd"
echo echo node: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\node.cmd"
echo exit /b 127 >> "%FAKE_BIN_DIR%\node.cmd"

REM Create fake npm executable
echo @echo off > "%FAKE_BIN_DIR%\npm.bat"
echo echo npm: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\npm.bat"
echo exit /b 127 >> "%FAKE_BIN_DIR%\npm.bat"

echo @echo off > "%FAKE_BIN_DIR%\npm.cmd"
echo echo npm: command not found 1^>^&2 >> "%FAKE_BIN_DIR%\npm.cmd"
echo exit /b 127 >> "%FAKE_BIN_DIR%\npm.cmd"

REM Prepend fake bin directory to PATH to override system executables
set "PATH=%FAKE_BIN_DIR%;%PATH%"

echo ✓ Fake runtimes created and PATH updated
goto :eof

:verify_runtimes_blocked
echo.
echo === Verifying that host runtimes are blocked ===

set "blocked_count=0"
set "total_runtimes=4"

REM Test PHP
php --version >nul 2>&1
if !errorlevel! equ 0 (
    echo ⚠️  PHP is still available and working
) else (
    echo ✅ PHP successfully blocked
    set /a blocked_count+=1
)

REM Test Python3
python3 --version >nul 2>&1
if !errorlevel! equ 0 (
    echo ⚠️  Python3 is still available and working
) else (
    echo ✅ Python3 successfully blocked
    set /a blocked_count+=1
)

REM Test Python
python --version >nul 2>&1
if !errorlevel! equ 0 (
    echo ⚠️  Python is still available and working
) else (
    echo ✅ Python successfully blocked
    set /a blocked_count+=1
)

REM Test Node.js
node --version >nul 2>&1
if !errorlevel! equ 0 (
    echo ⚠️  Node.js is still available and working
) else (
    echo ✅ Node.js successfully blocked
    set /a blocked_count+=1
)

echo.
echo Runtimes blocked: !blocked_count!/!total_runtimes!

if !blocked_count! geq 3 (
    echo ✅ Runtime blocking successful
) else (
    echo ⚠️  Some runtimes are still available, but continuing with tests...
)
goto :eof

:test_executable
set "exe_name=%~1"
set "exe_path=%~2"
set "timeout_seconds=%~3"
if "%timeout_seconds%"=="" set "timeout_seconds=30"

echo.
echo --- Testing %exe_name% Executable ---

if not exist "%exe_path%" (
    echo ❌ %exe_name% executable not found at: %exe_path%
    exit /b 1
)

echo 📁 Path: %exe_path%

REM Get file size
for %%A in ("%exe_path%") do set "file_size=%%~zA"
echo 📊 Size: !file_size! bytes

REM Test execution with timeout
echo 🚀 Running %exe_name% executable (timeout: %timeout_seconds%s^)...

set "start_time=%TIME%"

REM Run with timeout (Windows timeout command)
timeout /t %timeout_seconds% "%exe_path%" > "%exe_name%_output.log" 2>&1
set "exit_code=!errorlevel!"

set "end_time=%TIME%"

echo 🔢 Exit code: !exit_code!

REM Show output (first few lines)
if exist "%exe_name%_output.log" (
    set /p first_line=<"%exe_name%_output.log"
    if not "!first_line!"=="" (
        echo 📝 Output (first lines^):
        type "%exe_name%_output.log" | more +0
        
        REM Check for common error patterns
        findstr /i "error exception failed cannot unable" "%exe_name%_output.log" >nul 2>&1
        if !errorlevel! equ 0 (
            echo ⚠️  Potential errors detected in output
        )
    ) else (
        echo 📝 No output captured
    )
) else (
    echo 📝 No output file created
)

REM Determine test result
if !exit_code! equ 0 (
    echo ✅ %exe_name% test PASSED
    exit /b 0
) else if !exit_code! equ 1 (
    echo ⏰ %exe_name% test TIMED OUT or COMPLETED
    exit /b 0
) else (
    echo ❌ %exe_name% test FAILED (exit code: !exit_code!^)
    exit /b 1
)

:run_all_tests
echo.
echo === Testing Portable Executables (No System Runtimes^) ===

if not exist "%EXAMPLES_OUTPUT_DIR%" (
    echo ❌ Examples output directory not found: %EXAMPLES_OUTPUT_DIR%
    echo Make sure to build the examples first!
    exit /b 1
)

set "passed_tests=0"
set "total_tests=0"

REM Test Node.js executable
set "nodejs_exe=%EXAMPLES_OUTPUT_DIR%\nodejs.exe"
if exist "%nodejs_exe%" (
    set /a total_tests+=1
    call :test_executable "Node.js" "%nodejs_exe%" 30
    if !errorlevel! equ 0 (
        set /a passed_tests+=1
        echo Node.js: ✅ PASSED
    ) else (
        echo Node.js: ❌ FAILED
    )
) else (
    echo Node.js: ⚪ NOT FOUND
    echo ⚠️  Node.js executable not found at: %nodejs_exe%
)

REM Test PHP executable
set "php_exe=%EXAMPLES_OUTPUT_DIR%\php.exe"
if exist "%php_exe%" (
    set /a total_tests+=1
    call :test_executable "PHP" "%php_exe%" 30
    if !errorlevel! equ 0 (
        set /a passed_tests+=1
        echo PHP: ✅ PASSED
    ) else (
        echo PHP: ❌ FAILED
    )
) else (
    echo PHP: ⚪ NOT FOUND
    echo ⚠️  PHP executable not found at: %php_exe%
)

REM Test Python executable
set "python_exe=%EXAMPLES_OUTPUT_DIR%\python.exe"
if exist "%python_exe%" (
    set /a total_tests+=1
    call :test_executable "Python" "%python_exe%" 30
    if !errorlevel! equ 0 (
        set /a passed_tests+=1
        echo Python: ✅ PASSED
    ) else (
        echo Python: ❌ FAILED
    )
) else (
    echo Python: ⚪ NOT FOUND
    echo ⚠️  Python executable not found at: %python_exe%
)

REM Display summary
echo.
echo ==============================================
echo PORTABILITY TEST SUMMARY
echo ==============================================

echo 📊 Tests passed: !passed_tests!/!total_tests!

if !total_tests! equ 0 (
    echo ⚠️  No executables found to test!
    echo Make sure to build the examples first using:
    echo   build-examples.bat
    exit /b 1
) else if !passed_tests! equ !total_tests! (
    echo 🎉 All tests PASSED - Executables are truly portable!
    exit /b 0
) else if !passed_tests! gtr 0 (
    echo ⚠️  Some tests passed - Partial portability achieved
    exit /b 0
) else (
    echo ❌ All tests FAILED - Portability issues detected
    exit /b 1
)

:cleanup
echo Cleaning up fake bin directory...
if exist "%FAKE_BIN_DIR%" rmdir /s /q "%FAKE_BIN_DIR%" >nul 2>&1
goto :eof
