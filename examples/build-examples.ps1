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
    
    if (Test-Path $BuildDir) {
        Write-Log "Cleaning previous build..."
        Remove-Item -Recurse -Force $BuildDir
    }
    
    Write-Log "Creating build directory..."
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    Set-Location $BuildDir
    
    Write-Log "Configuring with CMake..."
    try {
        cmake -DCMAKE_BUILD_TYPE=Release $ProjectRoot
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configuration failed"
        }
    }
    catch {
        Write-Log "CMake configuration failed: $_" -Level Error
        return $false
    }
    
    Write-Log "Building UBuilder..."
    try {
        cmake --build . --config Release
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed"
        }
    }
    catch {
        Write-Log "Build failed: $_" -Level Error
        return $false
    }
    
    # Check for executable
    $UBuilderExe = Join-Path $BuildDir "src\Release\ubuilder.exe"
    if (-not (Test-Path $UBuilderExe)) {
        $UBuilderExe = Join-Path $BuildDir "src\ubuilder.exe"
        if (-not (Test-Path $UBuilderExe)) {
            Write-Log "UBuilder executable not found after build" -Level Error
            return $false
        }
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
    try {
        & $UBuilderExe --project-dir="$ExampleDir" --runtime=$Runtime --output="$OutputExecutable"
        if ($LASTEXITCODE -ne 0) {
            throw "UBuilder execution failed"
        }
    }
    catch {
        Write-Log "Failed to build $ExampleName`: $_" -Level Error
        return @{ Success = $false; Reason = "BuildError" }
    }
    
    if (-not (Test-Path $OutputExecutable)) {
        Write-Log "Executable not created: $OutputExecutable" -Level Error
        return @{ Success = $false; Reason = "BuildError" }
    fi
    
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
