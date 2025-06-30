# UBuilder Examples Build and Test Script for Windows (PowerShell)
# Builds UBuilder core, creates executables from all examples, and runs them

param(
    [switch]$Verbose = $false
)

# Set error action preference
$ErrorActionPreference = "Stop"

# Project paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $ProjectRoot "build"
$ExamplesDir = Join-Path $ProjectRoot "examples"
$OutputDir = Join-Path $ExamplesDir "output"

# Colors for output
$ColorInfo = "Cyan"
$ColorSuccess = "Green"
$ColorWarning = "Yellow"
$ColorError = "Red"
$ColorHeader = "Magenta"

function Write-Log {
    param(
        [string]$Message,
        [string]$Level = "Info"
    )
    
    switch ($Level) {
        "Info" { Write-Host "[INFO] $Message" -ForegroundColor $ColorInfo }
        "Success" { Write-Host "[SUCCESS] $Message" -ForegroundColor $ColorSuccess }
        "Warning" { Write-Host "[WARNING] $Message" -ForegroundColor $ColorWarning }
        "Error" { Write-Host "[ERROR] $Message" -ForegroundColor $ColorError }
        "Header" { 
            Write-Host "===========================================" -ForegroundColor $ColorHeader
            Write-Host " $Message" -ForegroundColor $ColorHeader
            Write-Host "===========================================" -ForegroundColor $ColorHeader
        }
    }
}

function Test-CommandExists {
    param([string]$Command)
    
    try {
        if (Get-Command $Command -ErrorAction Stop) {
            return $true
        }
    }
    catch {
        return $false
    }
}

function Test-Runtime {
    param([string]$Runtime)
    
    switch ($Runtime.ToLower()) {
        "python" {
            if (Test-CommandExists "python") {
                $version = python --version 2>&1
                Write-Log "Python runtime available: $version" -Level Success
                return $true
            }
            elseif (Test-CommandExists "python3") {
                $version = python3 --version 2>&1
                Write-Log "Python3 runtime available: $version" -Level Success
                return $true
            }
            else {
                Write-Log "Python runtime not available - install from https://python.org" -Level Warning
                return $false
            }
        }
        "php" {
            if (Test-CommandExists "php") {
                $version = (php --version 2>&1) -split "`n" | Select-Object -First 1
                Write-Log "PHP runtime available: $version" -Level Success
                return $true
            }
            else {
                Write-Log "PHP runtime not available - install from https://php.net" -Level Warning
                return $false
            }
        }
        "node" {
            if (Test-CommandExists "node") {
                $version = node --version 2>&1
                Write-Log "Node.js runtime available: $version" -Level Success
                return $true
            }
            else {
                Write-Log "Node.js runtime not available - install from https://nodejs.org" -Level Warning
                return $false
            }
        }
        default {
            Write-Log "Unknown runtime: $Runtime" -Level Error
            return $false
        }
    }
}

function Build-UBuilder {
    Write-Log "Building UBuilder Core (Windows)" -Level Header
    
    Set-Location $ProjectRoot
    
    # Check if UBuilder already exists and is recent
    $UBuilderExe = Join-Path $BuildDir "src\Release\ubuilder.exe"
    if (-not (Test-Path $UBuilderExe)) {
        $UBuilderExe = Join-Path $BuildDir "src\ubuilder.exe"
    }
    
    if (Test-Path $UBuilderExe) {
        $exeTime = (Get-Item $UBuilderExe).LastWriteTime
        $sourceTime = (Get-ChildItem -Path "src" -Recurse -Include "*.c", "*.h" | Sort-Object LastWriteTime -Descending | Select-Object -First 1).LastWriteTime
        
        if ($exeTime -gt $sourceTime) {
            Write-Log "UBuilder executable is up to date, skipping rebuild..." -Level Success
            return $UBuilderExe
        } else {
            Write-Log "Source files are newer, rebuilding..."
        }
    }
    
    if (Test-Path $BuildDir) {
        Write-Log "Cleaning previous build..."
        Remove-Item -Recurse -Force $BuildDir
    }
    
    Write-Log "Creating build directory..."
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    Set-Location $BuildDir
    
    Write-Log "Configuring with CMake..."
    try {
        # Use Start-Process for better output control
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = "cmake"
        $psi.Arguments = "-DCMAKE_BUILD_TYPE=Release `"$ProjectRoot`""
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.WorkingDirectory = $BuildDir
        
        $process = [System.Diagnostics.Process]::Start($psi)
        $cmakeOutput = $process.StandardOutput.ReadToEnd()
        $cmakeError = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        
        if ($process.ExitCode -ne 0) {
            Write-Log "CMake configuration failed with exit code $($process.ExitCode)" -Level Error
            if ($cmakeError) { Write-Log "CMake Error: $cmakeError" -Level Error }
            if ($cmakeOutput) { Write-Log "CMake Output: $cmakeOutput" -Level Error }
            return $null
        }
        Write-Verbose "CMake configuration successful"
    }
    catch {
        Write-Log "CMake configuration failed: $_" -Level Error
        return $null
    }
    
    Write-Log "Building UBuilder..."
    try {
        # Use Start-Process for build as well
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = "cmake"
        $psi.Arguments = "--build . --config Release"
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.WorkingDirectory = $BuildDir
        
        $process = [System.Diagnostics.Process]::Start($psi)
        $buildOutput = $process.StandardOutput.ReadToEnd()
        $buildError = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        
        if ($process.ExitCode -ne 0) {
            Write-Log "Build failed with exit code $($process.ExitCode)" -Level Error
            if ($buildError) { Write-Log "Build Error: $buildError" -Level Error }
            if ($buildOutput) { Write-Log "Build Output: $buildOutput" -Level Error }
            return $null
        }
        Write-Verbose "Build successful"
    }
    catch {
        Write-Log "Build failed: $_" -Level Error
        return $null
    }
    
    # Check for executable in different possible locations
    $PossiblePaths = @(
        (Join-Path $BuildDir "src\Release\ubuilder.exe"),
        (Join-Path $BuildDir "src\Debug\ubuilder.exe"),
        (Join-Path $BuildDir "src\ubuilder.exe"),
        (Join-Path $BuildDir "Release\ubuilder.exe"),
        (Join-Path $BuildDir "Debug\ubuilder.exe"),
        (Join-Path $BuildDir "ubuilder.exe")
    )
    
    $UBuilderExe = $null
    foreach ($Path in $PossiblePaths) {
        if (Test-Path $Path) {
            $UBuilderExe = $Path
            Write-Log "Found UBuilder executable at: $UBuilderExe" -Level Success
            break
        }
    }
    
    if (-not $UBuilderExe) {
        Write-Log "UBuilder executable not found after build in any of the expected locations:" -Level Error
        foreach ($Path in $PossiblePaths) {
            Write-Log "  - $Path" -Level Error
        }
        
        # List what files are actually in the build directory for debugging
        Write-Log "Files in build directory:" -Level Error
        if (Test-Path $BuildDir) {
            Get-ChildItem -Recurse $BuildDir -Include "*.exe" | ForEach-Object {
                Write-Log "  - $($_.FullName)" -Level Error
            }
        }
        return $null
    }
    
    Write-Log "UBuilder core built successfully" -Level Success
    return $UBuilderExe
}

function Build-Example {
    param(
        [string]$ExampleDir,
        [string]$UBuilderExe
    )
    
    $ExampleName = Split-Path -Leaf $ExampleDir
    $OutputExecutable = Join-Path $OutputDir "$ExampleName.exe"
    
    Write-Log "Building example: $ExampleName"
    
    # Read runtime from ubuilder.json
    $ConfigPath = Join-Path $ExampleDir "ubuilder.json"
    if (-not (Test-Path $ConfigPath)) {
        Write-Log "No ubuilder.json found in $ExampleDir" -Level Error
        return @{ Success = $false; Reason = "NoConfig" }
    }
    
    try {
        $Config = Get-Content $ConfigPath | ConvertFrom-Json
        $Runtime = $Config.runtime
    }
    catch {
        Write-Log "Failed to parse ubuilder.json in $ExampleDir" -Level Error
        return @{ Success = $false; Reason = "ConfigError" }
    }
    
    # Check if runtime is available
    if (-not (Test-Runtime $Runtime)) {
        Write-Log "Skipping $ExampleName due to missing $Runtime runtime" -Level Warning
        return @{ Success = $false; Reason = "MissingRuntime" }
    }
    
    # Build the example
    Write-Log "Creating executable for $ExampleName (runtime: $Runtime)..."
    
    # Use proper output capture to avoid mixing build output with error messages
    $buildOutput = ""
    $buildError = ""
    $exitCode = 0
    
    try {
        # Capture both stdout and stderr separately
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $UBuilderExe
        $psi.Arguments = "--project-dir=`"$ExampleDir`" --runtime=$Runtime --output=`"$OutputExecutable`""
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.WorkingDirectory = $PWD.Path
        
        $process = [System.Diagnostics.Process]::Start($psi)
        
        # Read output asynchronously to prevent deadlocks
        $buildOutput = $process.StandardOutput.ReadToEnd()
        $buildError = $process.StandardError.ReadToEnd()
        
        $process.WaitForExit()
        $exitCode = $process.ExitCode
        
        if ($exitCode -ne 0) {
            Write-Log "UBuilder failed with exit code $exitCode" -Level Error
            if ($buildError) {
                Write-Log "Error output: $buildError" -Level Error
            }
            if ($buildOutput) {
                Write-Log "Build output: $buildOutput" -Level Error
            }
            return @{ Success = $false; Reason = "BuildError" }
        }
        
        # Success - show any relevant output
        if ($buildOutput -and $buildOutput.Trim()) {
            Write-Verbose "Build output: $buildOutput"
        }
        
    }
    catch {
        Write-Log "Failed to execute UBuilder: $_" -Level Error
        return @{ Success = $false; Reason = "BuildError" }
    }
    
    if (-not (Test-Path $OutputExecutable)) {
        Write-Log "Executable not created: $OutputExecutable" -Level Error
        return @{ Success = $false; Reason = "BuildError" }
    }
    
    Write-Log "Built $ExampleName -> $OutputExecutable" -Level Success
    return @{ Success = $true; Executable = $OutputExecutable }
}

function Test-Example {
    param([string]$Executable)
    
    $ExampleName = [System.IO.Path]::GetFileNameWithoutExtension($Executable)
    
    Write-Log "Running $ExampleName..."
    
    if (-not (Test-Path $Executable)) {
        Write-Log "Executable not found: $Executable" -Level Error
        return $false
    }
    
    Write-Host "--- Output from $ExampleName ---" -ForegroundColor Cyan
    try {
        & $Executable
        if ($LASTEXITCODE -ne 0) {
            throw "Execution failed with exit code $LASTEXITCODE"
        }
    }
    catch {
        Write-Host "--- End output ---" -ForegroundColor Cyan
        Write-Log "Failed to run $ExampleName`: $_" -Level Error
        return $false
    }
    Write-Host "--- End output ---" -ForegroundColor Cyan
    
    Write-Log "Successfully ran $ExampleName" -Level Success
    return $true
}

# Main execution
function Main {
    Write-Log "UBuilder Examples Build and Test (Windows)" -Level Header
    
    # Check for required tools
    if (-not (Test-CommandExists "cmake")) {
        Write-Log "CMake not found. Please install CMake and add it to PATH." -Level Error
        exit 1
    }
    
    # Create output directory
    if (-not (Test-Path $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }
    
    # Step 1: Build UBuilder core
    $UBuilderExe = Build-UBuilder
    if (-not $UBuilderExe) {
        Write-Log "Failed to build UBuilder core. Aborting." -Level Error
        exit 1
    }
    
    # Validate that the UBuilder executable path is valid
    if (-not (Test-Path $UBuilderExe)) {
        Write-Log "UBuilder executable path is invalid: $UBuilderExe" -Level Error
        exit 1
    }
    
    # Validate that it's actually an executable file
    if (-not $UBuilderExe.EndsWith(".exe")) {
        Write-Log "UBuilder path does not point to an executable: $UBuilderExe" -Level Error
        exit 1
    }
    
    Write-Log "Using UBuilder executable: $UBuilderExe" -Level Success
    
    # Step 2: Find and build all examples
    Write-Log "Building All Examples" -Level Header
    
    $Examples = Get-ChildItem -Path $ExamplesDir -Directory | Where-Object { 
        Test-Path (Join-Path $_.FullName "ubuilder.json") 
    }
    
    if ($Examples.Count -eq 0) {
        Write-Log "No example projects found" -Level Warning
        exit 0
    }
    
    Write-Log "Found $($Examples.Count) example projects"
    
    $BuiltExamples = @()
    $SkippedExamples = @()
    $FailedExamples = @()
    
    foreach ($Example in $Examples) {
        $Result = Build-Example -ExampleDir $Example.FullName -UBuilderExe $UBuilderExe
        
        if ($Result.Success) {
            $BuiltExamples += $Result.Executable
        }
        elseif ($Result.Reason -eq "MissingRuntime") {
            $SkippedExamples += $Example.Name
        }
        else {
            $FailedExamples += $Example.Name
        }
    }
    
    # Step 3: Run all successfully built examples
    if ($BuiltExamples.Count -gt 0) {
        Write-Log "Running All Built Examples" -Level Header
        
        $RunSuccessful = @()
        $RunFailed = @()
        
        foreach ($Executable in $BuiltExamples) {
            if (Test-Example $Executable) {
                $RunSuccessful += [System.IO.Path]::GetFileNameWithoutExtension($Executable)
            }
            else {
                $RunFailed += [System.IO.Path]::GetFileNameWithoutExtension($Executable)
            }
        }
        
        # Final summary
        Write-Log "Build and Test Summary (Windows)" -Level Header
        
        Write-Host "Total examples found: $($Examples.Count)" -ForegroundColor Blue
        Write-Host "Successfully built: $($BuiltExamples.Count)" -ForegroundColor Green
        Write-Host "Skipped (missing runtime): $($SkippedExamples.Count)" -ForegroundColor Yellow
        Write-Host "Build failures: $($FailedExamples.Count)" -ForegroundColor Red
        Write-Host "Successfully ran: $($RunSuccessful.Count)" -ForegroundColor Green
        Write-Host "Runtime failures: $($RunFailed.Count)" -ForegroundColor Red
        
        if ($BuiltExamples.Count -gt 0) {
            Write-Host "`nBuilt executables:" -ForegroundColor Green
            foreach ($Executable in $BuiltExamples) {
                Write-Host "  • $Executable"
            }
        }
        
        if ($SkippedExamples.Count -gt 0) {
            Write-Host "`nSkipped examples (missing runtime):" -ForegroundColor Yellow
            foreach ($Example in $SkippedExamples) {
                Write-Host "  • $Example"
            }
        }
        
        if ($FailedExamples.Count -gt 0) {
            Write-Host "`nFailed builds:" -ForegroundColor Red
            foreach ($Example in $FailedExamples) {
                Write-Host "  • $Example"
            }
        }
        
        if ($RunFailed.Count -gt 0) {
            Write-Host "`nRuntime failures:" -ForegroundColor Red
            foreach ($Example in $RunFailed) {
                Write-Host "  • $Example"
            }
        }
        
        # Overall success check
        if ($RunSuccessful.Count -eq $BuiltExamples.Count -and $BuiltExamples.Count -gt 0) {
            Write-Host "`n🎉 ALL BUILDS AND TESTS SUCCESSFUL ON WINDOWS! 🎉" -ForegroundColor Green
            Write-Host "All $($BuiltExamples.Count) example(s) built and ran successfully!" -ForegroundColor Green
            exit 0
        }
        else {
            if ($BuiltExamples.Count -eq 0) {
                Write-Host "`n❌ No examples could be built" -ForegroundColor Red
            }
            else {
                Write-Host "`n⚠️  Some examples failed to run properly" -ForegroundColor Yellow
            }
            exit 1
        }
    }
    else {
        Write-Log "No examples were successfully built" -Level Error
        exit 1
    }
}

# Run main function
Main
