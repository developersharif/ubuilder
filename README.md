# UBuilder - Universal Executable Framework

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![CI](https://github.com/developersharif/ubuilder/workflows/UBuilder%20Cross-Platform%20CI/badge.svg)](https://github.com/developersharif/ubuilder/actions)
[![Build Scripts](https://github.com/developersharif/ubuilder/workflows/Build%20Scripts%20Validation/badge.svg)](https://github.com/developersharif/ubuilder/actions)
[![Linux](https://img.shields.io/badge/platform-Linux-blue.svg)]()
[![macOS](https://img.shields.io/badge/platform-macOS-blue.svg)]()
[![Windows](https://img.shields.io/badge/platform-Windows-blue.svg)]()
[![Version](https://img.shields.io/badge/version-2.0.0-blue.svg)]()

> **Transform your Python, PHP, and Node.js applications into truly portable, dependency-free executables.**

UBuilder is a cross-platform C/C++ framework that packages dynamic language applications (Python, PHP, Node.js) into single, self-contained executables. No more "it works on my machine" - your applications run anywhere with zero dependencies.

## ✨ Key Features

- 🚀 **True Runtime Embedding**: Bundles complete interpreters (PHP, Python, Node.js)
- 📦 **Zero Dependencies**: Generated executables require no runtime installation
- 🌍 **Cross-Platform**: Works on Linux, macOS, and Windows
- 📁 **Multi-File Support**: Handles complex projects with imports/requires
- ⚡ **Fast Execution**: Optimized extraction and execution pipeline
- 🔧 **Simple CLI**: Easy-to-use command-line interface

## 🚀 Quick Start

### Prerequisites

- CMake 3.16+
- C compiler (GCC, Clang, or MSVC)
- Target runtime installed (PHP, Python, or Node.js)

### Build UBuilder

#### Quick Start (All Platforms)

```bash
# Universal build command (detects platform automatically)
./build-all.sh

# Or build specific components
mkdir build && cd build
cmake .. && make
./src/ubuilder --version
```

#### Platform-Specific Build Commands

**Linux/Unix:**

```bash
./examples/build-examples-linux.sh
```

**macOS:**

```bash
./examples/build-examples-macos.sh
```

**Windows:**

```batch
# Command Prompt
examples\build-examples.bat

# PowerShell
examples\build-examples.ps1

# WSL/MSYS2/Git Bash
./examples/build-examples.sh
```

See [examples/BUILD_SYSTEM.md](examples/BUILD_SYSTEM.md) for detailed cross-platform build instructions.

### Create Your First Portable Executable

```bash
# PHP Application
./build/src/ubuilder --project-dir=./examples/php-hello --runtime=php --output=my-php-app

# Python Application
./build/src/ubuilder --project-dir=./examples/python-hello --runtime=python --output=my-python-app

# Node.js Application
./build/src/ubuilder --project-dir=./examples/node-hello --runtime=node --output=my-node-app

# Run anywhere - no dependencies needed!
./my-php-app
```

## Project Structure

```
ubuilder/
├── src/                    # Core C/C++ source code
├── runtimes/              # Embedded runtime environments
├── build-system/          # Build automation scripts
├── examples/              # Sample applications
├── tests/                 # Test suite
├── docs/                  # Documentation
└── tools/                 # Development utilities
```

## Development Status

Currently in **Phase 1: Foundation** - Basic embedding and Python runtime support.

See [ProjectInstructions.md](ProjectInstructions.md) for detailed project roadmap.

## License

MIT License - see [LICENSE](LICENSE) for details.
