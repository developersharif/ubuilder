# UBuilder Cross-Platform Build System

This document describes the cross-platform build system for UBuilder, supporting Linux, macOS, and Windows.

## Quick Start

### Universal Build Command

```bash
# Works on Linux, macOS, and Unix-like systems
./build-all.sh
```

### Platform-Specific Commands

#### Linux

```bash
./examples/build-examples-linux.sh
```

#### macOS

```bash
./examples/build-examples-macos.sh
```

#### Windows

```batch
# Command Prompt
examples\build-examples.bat

# PowerShell
examples\build-examples.ps1

# WSL/MSYS2/Git Bash
./examples/build-examples.sh
```

## Build Scripts Overview

### Main Scripts

- **`build-all.sh`** - Universal wrapper that detects platform and runs appropriate script
- **`build-all.bat`** - Windows batch wrapper

### Platform-Specific Build Scripts

- **`examples/build-examples.sh`** - Universal script with platform detection
- **`examples/build-examples-linux.sh`** - Linux-optimized build script
- **`examples/build-examples-macos.sh`** - macOS-optimized build script
- **`examples/build-examples.bat`** - Windows batch script
- **`examples/build-examples.ps1`** - Windows PowerShell script

## What the Build System Does

1. **Detects Platform** - Automatically identifies the operating system
2. **Builds UBuilder Core** - Compiles the main UBuilder executable using CMake
3. **Discovers Examples** - Finds all example projects with `ubuilder.json` files
4. **Checks Runtimes** - Verifies availability of Python, PHP, and Node.js
5. **Creates Executables** - Uses UBuilder to package examples into standalone executables
6. **Tests Executables** - Runs each generated executable to verify functionality
7. **Reports Results** - Provides detailed summary of build and test results

## Prerequisites

### All Platforms

- CMake 3.16+
- C compiler (GCC, Clang, or MSVC)

### Linux

- Build essentials: `sudo apt install build-essential cmake`
- Optional runtimes: `sudo apt install python3 php nodejs`

### macOS

- Xcode Command Line Tools: `xcode-select --install`
- Homebrew (recommended): `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"`
- Optional runtimes: `brew install python php node`

### Windows

- Visual Studio 2019+ or Build Tools
- CMake (add to PATH)
- Optional runtimes:
  - Python: https://python.org
  - PHP: https://php.net
  - Node.js: https://nodejs.org

## Output Structure

After successful build, you'll find:

```
examples/
├── output/
│   ├── nodejs      # Node.js executable (Linux/macOS)
│   ├── nodejs.exe  # Node.js executable (Windows)
│   ├── php         # PHP executable (Linux/macOS)
│   ├── php.exe     # PHP executable (Windows)
│   ├── python      # Python executable (Linux/macOS)
│   └── python.exe  # Python executable (Windows)
└── build-examples.*
```

## Platform-Specific Features

### Linux

- Uses `nproc` for optimal parallel builds
- Supports standard package managers
- Optimized for typical Linux development environments

### macOS

- Uses `sysctl -n hw.ncpu` for CPU count
- Sets `CMAKE_OSX_DEPLOYMENT_TARGET=10.15` for compatibility
- Provides Homebrew installation suggestions
- Handles macOS-specific runtime paths

### Windows

- Supports both Visual Studio and MinGW builds
- Batch script for traditional Windows workflows
- PowerShell script with advanced error handling and colored output
- Handles Windows-specific executable extensions (.exe)
- Supports multiple PowerShell execution policies

## Troubleshooting

### Common Issues

1. **CMake Not Found**

   - Linux: `sudo apt install cmake`
   - macOS: `brew install cmake`
   - Windows: Download from https://cmake.org and add to PATH

2. **Compiler Not Found**

   - Linux: `sudo apt install build-essential`
   - macOS: `xcode-select --install`
   - Windows: Install Visual Studio Build Tools

3. **Runtime Not Available**
   - Scripts will skip examples for missing runtimes
   - Install required runtimes as needed
   - Check runtime availability with `python --version`, `php --version`, `node --version`

### Debug Mode

Most scripts support verbose output. Check the specific script for debug options.

## Success Indicators

When everything works correctly, you'll see:

- ✅ Green success messages for each step
- 🎉 Final success message with build count
- Generated executables in `examples/output/`
- All executables run without errors

## Integration

The build system integrates with:

- **CI/CD pipelines** - Use appropriate script for each platform
- **IDEs** - Scripts can be run from integrated terminals
- **Package managers** - Works with system-installed dependencies
- **Cross-compilation** - Supports building for multiple platforms where possible

## Contributing

When adding new platforms or features:

1. Create platform-specific script if needed
2. Update the universal detection logic
3. Add platform-specific prerequisites to README
4. Test on actual platform hardware
5. Update the wrapper scripts accordingly
