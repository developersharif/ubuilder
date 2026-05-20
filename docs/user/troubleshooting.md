# UBuilder Troubleshooting Guide

## 🔧 Common Issues and Solutions

### Build Issues

#### CMake Configuration Errors

**Error:** `CMake not found`

```bash
# Solution: Install CMake
# Ubuntu/Debian
sudo apt-get install cmake

# macOS
brew install cmake

# Windows
# Download from https://cmake.org/download/
```

**Error:** `CMake version too old`

```bash
# Check current version
cmake --version

# Upgrade CMake (Ubuntu)
sudo apt-get update
sudo apt-get install cmake

# For older systems, install from source or snap
sudo snap install cmake --classic
```

**Error:** `Compiler not found`

```bash
# Install build tools
# Ubuntu/Debian
sudo apt-get install build-essential

# CentOS/RHEL
sudo yum groupinstall "Development Tools"

# macOS
xcode-select --install
```

#### UBuilder Build Failures

**Error:** `runtime_embedder.c: No such file or directory`

```bash
# Ensure all source files are present
ls -la src/runtimes/
# Should show runtime_embedder.c and runtime_embedder.h

# Clean and rebuild
cd build
make clean
cmake .. && make
```

**Error:** `undefined reference to 'ub_detect_runtime_binary'`

```bash
# Linker error - check CMakeLists.txt includes runtime_embedder.c
grep runtime_embedder src/CMakeLists.txt

# If missing, add to CMakeLists.txt:
# runtime_embedder.c
# runtime_embedder.h
```

### Runtime Detection Issues

#### Runtime Not Found

**Error:** `Runtime not found on system`

```bash
# Check if runtime is installed
which php python3 node

# Install missing runtimes
# Ubuntu/Debian
sudo apt-get install php-cli python3 nodejs

# macOS
brew install php python node

# CentOS/RHEL
sudo yum install php-cli python3 nodejs npm
```

**Error:** `Binary detection failed`

```bash
# Check PATH
echo $PATH

# Verify executable permissions
ls -la $(which php)
ls -la $(which python3)
ls -la $(which node)

# Test manual execution
php --version
python3 --version
node --version
```

**Error:** `Symlink resolution failed`

```bash
# Debug symlink chain
ls -la $(which php)
readlink -f $(which php)

# If broken symlinks, reinstall runtime
sudo apt-get remove --purge php-cli
sudo apt-get install php-cli
```

### Project Validation Issues

#### Entry Point Detection

**Error:** `Entry point file not found`

```bash
# Check project structure
ls -la your-project/

# UBuilder looks for these files (in order):
# PHP: main.php, index.php, *.php
# Python: main.py, __main__.py, app.py, *.py
# Node.js: main.js, index.js, package.json entry

# Create missing entry point
echo '<?php echo "Hello World\n"; ?>' > your-project/main.php
```

**Error:** `Project validation failed`

```bash
# Check file permissions
chmod 644 your-project/*.php
chmod 755 your-project/

# Verify project contains valid code
php -l your-project/main.php    # PHP syntax check
python3 -m py_compile your-project/main.py  # Python syntax check
node -c your-project/main.js    # Node.js syntax check
```

### Execution Issues

#### Embedded App Detection

**Error:** `--project-dir is required` (when running embedded app)

```bash
# This means embedded app detection failed
# Debug with:
strings your-app | grep UBUILDER

# Should show:
# UBUILDER_MODULAR_MARKER
# If missing, rebuild the application
```

**Error:** `No embedded data found`

```bash
# Check file size - should be > 1MB for embedded runtime
ls -lh your-app

# If file is small (~54KB), rebuild with verbose output
./ubuilder --project-dir=./your-project --runtime=php --output=your-app --verbose
```

#### Runtime Extraction Issues

**Error:** `Extraction failed`

```bash
# Check /tmp permissions and space
df -h /tmp
ls -ld /tmp

# If /tmp is full or read-only, set alternative temp directory
export TMPDIR=/home/user/tmp
mkdir -p $TMPDIR
./your-app
```

**Error:** `Permission denied on extracted binary`

```bash
# Check extraction directory
ps aux | grep your-app
# Look for /tmp/ubuilder-XXXXX

# Manually check permissions
ls -la /tmp/ubuilder-*/runtime_binary

# Should be executable (755)
# If not, this is a bug - report it
```

### Platform-Specific Issues

#### Linux Issues

**Error:** `GLIBC version mismatch`

```bash
# Check GLIBC version
ldd --version

# If too old, use static linking or upgrade system
# Or build on older system for better compatibility
```

**Error:** `shared library not found`

```bash
# Check library dependencies
ldd your-app

# If shows missing libraries, ensure system is compatible
# UBuilder should create self-contained executables
```

#### macOS Issues

**Error:** `dyld: Library not loaded`

```bash
# Check library paths
otool -L your-app

# If shows external dependencies, rebuild UBuilder
# Ensure proper static linking
```

**Error:** `Code signing issues`

```bash
# Disable Gatekeeper temporarily for testing
sudo spctl --master-disable

# Or sign the executable
codesign -s "Developer ID Application" your-app
```

#### Windows Issues

**Error:** `MSVCR*.dll not found`

```bash
# Install Visual C++ Redistributable
# Download from Microsoft website

# Or use static linking during build
# cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ..
```

**Issue: console window flashes briefly then disappears**

By default, ubuilder patches the output `.exe` to run as a Windows GUI subsystem app (no console window). If your app is a CLI tool that prints output, add `"console": true` to your `ubuilder.json`:

```json
{ "runtime": "node", "entry_point": "main.js", "console": true }
```

**Issue: Python runtime not found on Windows**

Windows users who installed Python via the Microsoft Store may have a stub
(`C:\Users\<name>\AppData\Local\Microsoft\WindowsApps\python.exe`) on PATH
that causes auto-detection to fail. Point ubuilder at your real installation:

```json
{
  "runtime": "python",
  "entry_point": "main.py",
  "runtime_options": {
    "python": { "source": "C:\\Users\\YourName\\AppData\\Local\\Programs\\Python\\Python311" }
  }
}
```

Or pass it on the CLI:

```powershell
ubuilder --runtime-source="C:\Users\YourName\AppData\Local\Programs\Python\Python311"
```

## 🔍 Debugging Techniques

### Verbose Output Analysis

```bash
# Build with verbose output
./ubuilder --project-dir=./app --runtime=php --output=app --verbose

# Look for these key messages:
# "Embedding PHP runtime: /path/to/php"  # Runtime detection
# "Binary size: X.XX MB"                 # Size verification
# "Successfully created modular PHP executable"  # Success confirmation
```

### System Call Tracing

```bash
# Trace embedded app execution
strace -f -e execve ./your-app 2>&1 | grep -E "(execve|runtime_binary)"

# Should show:
# execve("./your-app", ...)  # Main execution
# execve("/tmp/ubuilder-XXXX/runtime_binary", ...)  # Embedded runtime

# If missing runtime_binary call, extraction failed
```

### Memory and Performance Debugging

```bash
# Check memory usage
/usr/bin/time -v ./your-app

# Profile startup time
time ./your-app --version

# Check for memory leaks (if available)
valgrind --leak-check=full ./your-app
```

### File System Debugging

```bash
# Monitor file operations
# Terminal 1: Run app
./your-app &
APP_PID=$!

# Terminal 2: Monitor files
watch -n 1 "ls -la /tmp/ubuilder-*/ 2>/dev/null || echo 'No temp files'"

# Check cleanup
sleep 5
kill $APP_PID
ls /tmp/ubuilder-* 2>/dev/null || echo "Cleanup successful"
```

## 📊 Diagnostic Commands

### System Environment Check

```bash
#!/bin/bash
# system-check.sh
echo "=== UBuilder System Diagnostic ==="
echo

echo "Build Tools:"
cmake --version 2>/dev/null || echo "❌ CMake not found"
gcc --version 2>/dev/null | head -1 || echo "❌ GCC not found"
make --version 2>/dev/null | head -1 || echo "❌ Make not found"

echo
echo "Runtimes:"
php --version 2>/dev/null | head -1 || echo "❌ PHP not found"
python3 --version 2>/dev/null || echo "❌ Python3 not found"
node --version 2>/dev/null || echo "❌ Node.js not found"

echo
echo "System Info:"
echo "OS: $(uname -s)"
echo "Architecture: $(uname -m)"
echo "Kernel: $(uname -r)"

echo
echo "Disk Space:"
df -h /tmp | tail -1

echo
echo "UBuilder Status:"
if [ -f "./build/src/ubuilder" ]; then
    echo "✅ UBuilder built successfully"
    ./build/src/ubuilder --version
else
    echo "❌ UBuilder not built"
fi
```

### Application Diagnostic

```bash
#!/bin/bash
# app-check.sh
APP="$1"

if [ -z "$APP" ]; then
    echo "Usage: $0 <executable>"
    exit 1
fi

echo "=== Application Diagnostic: $APP ==="
echo

echo "File Info:"
ls -lh "$APP"
file "$APP"

echo
echo "Embedded Markers:"
strings "$APP" | grep -E "(UBUILDER|MARKER)" | head -5

echo
echo "Runtime Detection Test:"
strace -f -e execve "./$APP" 2>&1 | grep -E "(runtime_binary|execve)" | head -10

echo
echo "Dependencies:"
ldd "$APP" 2>/dev/null | head -10 || echo "Static executable (good!)"
```

## 🆘 Getting Help

### Before Reporting Issues

1. **Run system diagnostic:**

   ```bash
   bash system-check.sh > diagnostic.txt
   ```

2. **Capture verbose build output:**

   ```bash
   ./ubuilder --project-dir=./app --runtime=php --output=test --verbose > build.log 2>&1
   ```

3. **Test with example projects:**
   ```bash
   ./ubuilder --project-dir=./examples/php-hello --runtime=php --output=test-php
   ./test-php
   ```

### Information to Include in Bug Reports

- **System Information:**

  - OS and version
  - Architecture (x86_64, ARM, etc.)
  - Compiler version
  - CMake version

- **Runtime Information:**

  - PHP/Python/Node.js versions
  - Installation method (package manager, source, etc.)

- **Build Information:**

  - UBuilder version
  - Build commands used
  - Complete build output

- **Error Information:**
  - Complete error messages
  - Steps to reproduce
  - Expected vs actual behavior

### Community Resources

- **GitHub Issues:** Report bugs and feature requests
- **GitHub Discussions:** Ask questions and share tips
- **Wiki:** Community-maintained documentation
- **Examples Repository:** Additional example projects

### Quick Fixes Summary

| Issue              | Quick Fix                                       |
| ------------------ | ----------------------------------------------- |
| CMake not found    | `sudo apt-get install cmake`                    |
| Compiler not found | `sudo apt-get install build-essential`          |
| Runtime not found  | `sudo apt-get install php-cli python3 nodejs`   |
| Permission denied  | `chmod +x executable`                           |
| Extraction failed  | Check /tmp space: `df -h /tmp`                  |
| Build failed       | Clean rebuild: `make clean && cmake .. && make` |
| Detection failed   | Rebuild with: `--verbose`                       |
| Missing symbols    | Update CMakeLists.txt and rebuild               |

For persistent issues, create a minimal reproducible example and report it with full diagnostic information.
