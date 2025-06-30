# GitHub Actions CI/CD Integration

This document describes the comprehensive GitHub Actions workflows implemented for UBuilder's cross-platform build system.

## 🔄 Workflows Overview

### 1. **UBuilder Cross-Platform CI** (`.github/workflows/ci.yml`)
**Primary CI pipeline that tests builds across all supported platforms**

#### **Linux Jobs**
- **Matrix Strategy**: Tests on Ubuntu Latest and Ubuntu 20.04
- **Dependencies**: CMake, build-essential, zlib, PHP, Python3, Node.js
- **Build Process**: 
  - Runs universal `./build-all.sh` script
  - Tests platform-specific `build-examples-linux.sh`
  - Executes unit tests if available
- **Artifacts**: UBuilder executable + example applications
- **Validation**: Version/help checks, runtime verification

#### **Windows Jobs**
- **Matrix Strategy**: Tests both Command Prompt and PowerShell environments
- **Dependencies**: MSVC Build Tools, CMake, Python, Node.js, PHP (via winget/chocolatey)
- **Build Process**:
  - Runs `examples\build-examples.bat` for CMD environment
  - Runs `examples\build-examples.ps1` for PowerShell environment
- **Artifacts**: UBuilder.exe + example applications (.exe)
- **Validation**: Cross-shell compatibility testing

#### **macOS Jobs**
- **Matrix Strategy**: Tests on macOS Latest and macOS 12
- **Dependencies**: Homebrew, CMake, zlib, PHP, Python3, Node.js
- **Build Process**:
  - Runs universal `./build-all.sh` script
  - Tests platform-specific `build-examples-macos.sh`
- **Artifacts**: UBuilder executable + example applications
- **Validation**: Homebrew integration, runtime path verification

#### **Integration Test Job**
- **Cross-Platform Verification**: Downloads all artifacts from all platforms
- **Summary Generation**: Creates comprehensive build report
- **Dependency**: Runs after all platform builds complete

#### **Release Job**
- **Trigger**: Only on pushes to `main` branch
- **Package Creation**: Combines all platform executables into release package
- **Artifacts**: Cross-platform distribution archive

### 2. **Build Scripts Validation** (`.github/workflows/validate-scripts.yml`)
**Validates build script quality and syntax**

#### **Script Validation Job**
- **ShellCheck**: Validates Bash script syntax and best practices
- **Batch Validation**: Checks Windows batch file syntax
- **PowerShell Validation**: Validates PowerShell script syntax using AST parsing
- **Permission Checks**: Ensures scripts have proper executable permissions
- **Help Testing**: Tests script help/usage functionality

#### **Minimal Build Test**
- **Core Build Only**: Tests UBuilder core build without runtime dependencies
- **Error Handling**: Validates graceful failure when runtimes missing
- **Timeout Protection**: Prevents hanging builds

### 3. **Build Status** (`.github/workflows/badge.yml`)
**Updates build status tracking**

- **Trigger**: Runs after main CI workflow completion
- **Status Logging**: Records build results and metadata
- **Future Enhancement**: Ready for badge generation setup

## 🎯 Features

### **Platform Coverage**
- ✅ **Linux**: Ubuntu 20.04, Ubuntu Latest
- ✅ **Windows**: Command Prompt + PowerShell environments  
- ✅ **macOS**: macOS 12, macOS Latest

### **Build Validation**
- ✅ **Core Build**: CMake + Make/MSBuild compilation
- ✅ **Example Applications**: Python, PHP, Node.js executable generation
- ✅ **Runtime Testing**: Actual execution of generated executables
- ✅ **Cross-Shell Support**: CMD/PowerShell/Bash compatibility

### **Quality Assurance**
- ✅ **Script Linting**: ShellCheck for Bash scripts
- ✅ **Syntax Validation**: PowerShell AST parsing
- ✅ **Permission Checks**: Executable bit validation
- ✅ **Error Handling**: Graceful failure testing

### **Artifact Management**
- ✅ **Platform Binaries**: Separate artifacts per platform
- ✅ **Example Apps**: Generated executables for testing
- ✅ **Build Logs**: Error logs for debugging failures
- ✅ **Release Packages**: Combined cross-platform distributions

## 🚀 Triggering Workflows

### **Automatic Triggers**
```yaml
# Main CI - runs on pushes and PRs
on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

# Script validation - runs on build script changes
on:
  pull_request:
    paths:
      - 'examples/build-examples*'
      - 'build-all.*'
      - 'build-system/**'
      - '.github/workflows/**'
```

### **Manual Triggers**
```bash
# Via GitHub UI or CLI
gh workflow run "UBuilder Cross-Platform CI"
gh workflow run "Build Scripts Validation"
```

## 📊 Status Badges

The README now includes dynamic status badges:

```markdown
[![CI](https://github.com/developersharif/ubuilder/workflows/UBuilder%20Cross-Platform%20CI/badge.svg)](https://github.com/developersharif/ubuilder/actions)
[![Build Scripts](https://github.com/developersharif/ubuilder/workflows/Build%20Scripts%20Validation/badge.svg)](https://github.com/developersharif/ubuilder/actions)
```

## 🛠️ Local Testing

Before pushing, you can test workflows locally:

```bash
# Validate YAML syntax
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))"

# Test build scripts
./build-all.sh                           # Unix systems
examples\build-examples.bat              # Windows CMD
examples\build-examples.ps1              # Windows PowerShell

# Validate scripts
shellcheck examples/build-examples*.sh   # Bash validation
```

## 🔧 Maintenance

### **Adding New Platforms**
1. Add new job to `ci.yml` with platform-specific dependencies
2. Create platform-specific build script in `examples/`
3. Update platform detection in universal scripts
4. Add platform badge to README

### **Adding New Runtimes**
1. Update runtime detection in all build scripts
2. Add runtime installation to CI workflows
3. Create example project with `ubuilder.json`
4. Test across all platforms

### **Troubleshooting Failures**
1. Check workflow logs in GitHub Actions tab
2. Download build artifacts for debugging
3. Test locally with same dependencies
4. Check script validation workflow for syntax issues

This comprehensive CI/CD system ensures UBuilder works reliably across all supported platforms and provides confidence for contributors and users.
