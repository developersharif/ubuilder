# UBuilder Release Creation Script (PowerShell)
# Usage: .\create-release.ps1 -Version v2.0.2 [-Message "Bug fixes and improvements"]

param(
    [Parameter(Mandatory=$true)]
    [string]$Version,
    
    [Parameter(Mandatory=$false)]
    [string]$Message = "Release $Version"
)

# Function to print colored output
function Write-Status {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}

function Write-Warning {
    param([string]$Message)
    Write-Host "[WARNING] $Message" -ForegroundColor Yellow
}

function Write-Error {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

# Validate version format (now supports alpha/beta)
if ($Version -notmatch '^v\d+\.\d+(\.\d+)?(-[a-zA-Z0-9]+)?$') {
    Write-Error "Version must be in format v#.#.# or v#.#-suffix (e.g., v2.0.2, v2.0-alpha, v1.0.0-beta)"
    exit 1
}

Write-Status "Creating release $Version"

# Check if we're in a git repository
if (-not (Test-Path ".git")) {
    Write-Error "This script must be run from the root of a git repository"
    exit 1
}

# Check for uncommitted changes
$gitStatus = git status --porcelain
if ($gitStatus) {
    Write-Warning "You have uncommitted changes:"
    $gitStatus | ForEach-Object { Write-Host $_ }
    Write-Host ""
    $response = Read-Host "Do you want to continue? (y/N)"
    if ($response -notmatch '^[Yy]$') {
        Write-Error "Release cancelled"
        exit 1
    }
}

# Check if tag already exists
$existingTags = git tag -l
if ($existingTags -contains $Version) {
    Write-Error "Tag $Version already exists"
    exit 1
}

# Get current branch
$currentBranch = git branch --show-current
Write-Status "Current branch: $currentBranch"

# Update version information
Write-Status "Updating version information..."

# Update version in CMakeLists.txt if it exists
if (Test-Path "CMakeLists.txt") {
    # Extract numeric version (remove 'v' prefix)
    $numericVersion = $Version.Substring(1)
    
    # Update project version in CMakeLists.txt
    $cmakeContent = Get-Content "CMakeLists.txt"
    $updatedContent = $cmakeContent -replace 'project\([^)]*VERSION [^)]*\)', "project(UBuilder VERSION $numericVersion)"
    
    if ($cmakeContent -ne $updatedContent) {
        $updatedContent | Set-Content "CMakeLists.txt"
        Write-Status "Updated version in CMakeLists.txt"
    }
}

# Update version in source code header if it exists
if (Test-Path "src/core/ubuilder.h") {
    $headerContent = Get-Content "src/core/ubuilder.h"
    $updatedHeader = $headerContent -replace '#define UBUILDER_VERSION.*', "#define UBUILDER_VERSION `"$Version`""
    
    if ($headerContent -ne $updatedHeader) {
        $updatedHeader | Set-Content "src/core/ubuilder.h"
        Write-Status "Updated version in ubuilder.h"
    }
}

# Update CHANGELOG.md
Write-Status "Updating CHANGELOG.md..."
$currentDate = Get-Date -Format "yyyy-MM-dd"

if (Test-Path "CHANGELOG.md") {
    # Read existing changelog
    $existingChangelog = Get-Content "CHANGELOG.md" -Raw
    
    # Create new changelog content
    $newChangelog = @"
# Changelog

## [$Version] - $currentDate

### Added
- $Message

$($existingChangelog -replace '^# Changelog\r?\n', '')
"@
    
    $newChangelog | Set-Content "CHANGELOG.md"
    Write-Status "Updated CHANGELOG.md"
} else {
    # Create new CHANGELOG.md
    $newChangelog = @"
# Changelog

## [$Version] - $currentDate

### Added
- $Message

"@
    
    $newChangelog | Set-Content "CHANGELOG.md"
    Write-Status "Created CHANGELOG.md"
}

# Add changes to git
Write-Status "Staging changes..."
git add .

# Check if there are changes to commit
$stagedChanges = git diff --cached --name-only
if ($stagedChanges) {
    Write-Status "Committing version updates..."
    git commit -m "chore: bump version to $Version"
} else {
    Write-Status "No version files to update"
}

# Create and push tag
Write-Status "Creating tag $Version..."
git tag -a $Version -m $Message

Write-Status "Pushing changes and tag to origin..."
git push origin $currentBranch
git push origin $Version

Write-Success "Release $Version created successfully!"
Write-Success "GitHub Actions will now build and publish the release automatically."
Write-Success "Check the Actions tab on GitHub to monitor the release process."

# Get repository URL for release link
$remoteUrl = git config remote.origin.url
$repoPath = if ($remoteUrl -match 'github\.com[:/](.*)\.git') { $matches[1] } else { "your-repo" }
Write-Success "Release will be available at: https://github.com/$repoPath/releases/tag/$Version"

Write-Host ""
Write-Status "Next steps:"
Write-Host "1. Monitor the GitHub Actions workflow"
Write-Host "2. Once complete, visit the Releases page to verify the release"
Write-Host "3. Test the published binaries if needed"
Write-Host "4. Share the release with users!"
