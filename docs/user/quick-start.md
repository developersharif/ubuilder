# UBuilder Quick Start Guide

## 🚀 Get Started in 5 Minutes

This guide will help you build and run your first portable executable with UBuilder.

## Prerequisites

Before you begin, ensure you have:

- **CMake 3.16+** - Build system
- **C Compiler** - GCC 7+, Clang 6+, or MSVC 2019+
- **Target Runtime** - At least one of: PHP 7.4+, Python 3.7+, Node.js 14+

### System Requirements

| Platform    | Minimum       | Recommended   |
| ----------- | ------------- | ------------- |
| **Linux**   | Ubuntu 18.04+ | Ubuntu 20.04+ |
| **macOS**   | 10.15+        | 12.0+         |
| **Windows** | Windows 10    | Windows 11    |

## Step 1: Build UBuilder

### Option A: Standard Build

```bash
# Clone and enter directory
git clone https://github.com/yourorg/ubuilder.git
cd ubuilder

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j$(nproc)

# Verify build
./src/ubuilder --version
```

### Option B: Development Build (with tests)

```bash
mkdir build && cd build
cmake -DBUILD_TESTS=ON ..
make -j$(nproc)
make test
```

## Step 2: Prepare Your Application

UBuilder works with existing applications in these structures:

### PHP Project Structure

```
my-php-app/
├── main.php          # Entry point (or index.php)
├── config.php        # Configuration
├── lib/              # Libraries
│   ├── database.php
│   └── utils.php
└── data/             # Data files
    └── config.json
```

### Python Project Structure

```
my-python-app/
├── main.py           # Entry point
├── requirements.txt  # Dependencies (optional)
├── modules/          # Local modules
│   ├── __init__.py
│   ├── core.py
│   └── utils.py
└── assets/           # Resources
    └── data.json
```

### Node.js Project Structure

```
my-node-app/
├── main.js           # Entry point (or index.js)
├── package.json      # Package definition
├── lib/              # Libraries
│   ├── server.js
│   └── routes.js
└── public/           # Static files
    └── style.css
```

## Step 3: Build Your Executable

### Basic Commands

```bash
# PHP Application
./build/src/ubuilder \
  --project-dir=./examples/php-hello \
  --runtime=php \
  --output=my-app

# Python Application
./build/src/ubuilder \
  --project-dir=./examples/python-hello \
  --runtime=python \
  --output=my-app

# Node.js Application
./build/src/ubuilder \
  --project-dir=./examples/node-hello \
  --runtime=node \
  --output=my-app
```

### Advanced Options

```bash
# Custom entry point
./build/src/ubuilder \
  --project-dir=./my-app \
  --runtime=php \
  --entry-point=bootstrap.php \
  --output=my-app

# Verbose output for debugging
./build/src/ubuilder \
  --project-dir=./my-app \
  --runtime=python \
  --output=my-app \
  --verbose
```

## Step 4: Test Your Executable

```bash
# Check the executable size
ls -lh my-app

# Run your portable application
./my-app

# Test on different machines (copy and run)
scp my-app user@remote-server:
ssh user@remote-server './my-app'
```

## Step 5: Distribute Your Application

Your executable is now completely portable:

✅ **No runtime dependencies** - Works on any compatible system  
✅ **Single file** - Easy to distribute and deploy  
✅ **Self-contained** - Includes everything needed to run

## 🔍 Verification

### Check what's embedded:

```bash
# View file structure
file my-app

# Check runtime embedding (should show embedded runtime calls)
strace -f -e execve ./my-app 2>&1 | grep runtime_binary
```

### Performance benchmarks:

```bash
# Startup time
time ./my-app --version

# Memory usage
/usr/bin/time -v ./my-app
```

## 🐛 Troubleshooting

### Build Issues

**CMake not found:**

```bash
# Ubuntu/Debian
sudo apt-get install cmake

# macOS
brew install cmake

# Windows
# Download from https://cmake.org/download/
```

**Compiler errors:**

```bash
# Install build tools (Ubuntu)
sudo apt-get install build-essential

# Install Xcode tools (macOS)
xcode-select --install
```

### Runtime Issues

**Runtime not detected:**

```bash
# Check if runtime is installed and in PATH
which php python3 node

# Install missing runtimes
sudo apt-get install php-cli python3 nodejs  # Ubuntu
brew install php python node                # macOS
```

**Executable won't run:**

```bash
# Check permissions
chmod +x my-app

# Check architecture compatibility
file my-app
uname -m
```

## 📚 Next Steps

- [📖 CLI Reference](cli-reference.md) - Complete command documentation
- [🏗️ Architecture](architecture.md) - How UBuilder works internally
- [💡 Examples](examples.md) - Real-world usage examples
- [🔧 Advanced Usage](advanced-usage.md) - Power user features

## 💬 Getting Help

- [🐛 Issues](https://github.com/yourorg/ubuilder/issues) - Report bugs
- [💡 Discussions](https://github.com/yourorg/ubuilder/discussions) - Ask questions
- [📖 Wiki](https://github.com/yourorg/ubuilder/wiki) - Community docs
