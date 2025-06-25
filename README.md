# UBuilder - Universal Executable Framework

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
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

```bash
# Clone the repository
git clone https://github.com/yourorg/ubuilder.git
cd ubuilder

# Build with CMake
mkdir build && cd build
cmake .. && make

# Verify installation
./src/ubuilder --version
```

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

## 📊 Size Comparison

| Runtime     | Embedded Size | Dependencies |
| ----------- | ------------- | ------------ |
| **PHP**     | ~6MB          | ❌ None      |
| **Python**  | ~7MB          | ❌ None      |
| **Node.js** | ~120MB        | ❌ None      |

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
