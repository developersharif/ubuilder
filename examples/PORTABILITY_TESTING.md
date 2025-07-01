# UBuilder Portability Testing Scripts

This directory contains cross-platform scripts to test the portability of UBuilder-generated executables by simulating environments where the host runtime dependencies (PHP, Python, Node.js) are not available.

## Scripts Overview

### `test-examples-linux-no-runtime.sh`

- **Platform**: Linux/Unix systems
- **Purpose**: Tests all built example executables on Linux without host runtimes
- **Method**: Creates fake runtime executables that simulate missing dependencies by returning "command not found" errors

### `test-examples-macos-no-runtime.sh`

- **Platform**: macOS systems
- **Purpose**: Tests all built example executables on macOS without host runtimes
- **Method**: Creates fake runtime executables and manipulates PATH to override system runtimes

### `test-examples-windows-no-runtime.bat`

- **Platform**: Windows systems
- **Purpose**: Tests all built example executables on Windows without host runtimes
- **Method**: Creates fake batch/cmd files to simulate missing runtime dependencies

## How They Work

1. **Runtime Blocking**: Each script creates fake executables that override the system's real runtime executables (php, python3, python, node, npm) by:

   - Creating temporary fake executables that return "command not found" errors
   - Prepending a temporary directory to the PATH environment variable
   - This simulates a clean environment without any runtime dependencies

2. **Verification**: Scripts verify that the runtimes are successfully blocked by attempting to run version commands and checking for failures

3. **Testing**: Scripts then attempt to run the UBuilder-generated executables (typically in `examples/output/`) and measure:

   - Execution success/failure
   - Runtime duration
   - Exit codes
   - Output capture and analysis

4. **Reporting**: Comprehensive test results showing which executables passed, failed, or were not found

## Usage

### Prerequisites

- UBuilder project must be built first (`./build-all.sh`, `build-all.bat`)
- Example applications must be compiled (`./examples/build-examples*.sh`, `examples/build-examples*.bat`)

### Running Tests

**Linux:**

```bash
cd examples
chmod +x test-examples-linux-no-runtime.sh
./test-examples-linux-no-runtime.sh
```

**macOS:**

```bash
cd examples
chmod +x test-examples-macos-no-runtime.sh
./test-examples-macos-no-runtime.sh
```

**Windows:**

```cmd
cd examples
test-examples-windows-no-runtime.bat
```

## Expected Results

### Success Cases

- Exit code 0: Executable runs successfully without host runtime dependencies
- Exit code 124 (Linux/macOS): Executable times out (acceptable for long-running apps)
- "✅ PASSED" status in the output

### Failure Cases

- Non-zero exit codes (except timeout): Executable failed to run properly
- "❌ FAILED" status indicating portability issues
- "⚪ NOT FOUND" status indicating the executable wasn't built

### Sample Output

```
=== UBuilder Linux Portability Test (No Runtimes) ===
✅ PHP successfully blocked
✅ Python3 successfully blocked
✅ Python successfully blocked
✅ Node.js successfully blocked

--- Testing Node.js Executable ---
📁 Path: examples/output/nodejs
📊 Size: 2.1M
🚀 Running Node.js executable (timeout: 30s)...
✅ Node.js test PASSED

=== PORTABILITY TEST SUMMARY ===
Node.js: ✅ PASSED
PHP: ✅ PASSED
Python: ✅ PASSED
📊 Tests passed: 3/3
🎉 All tests PASSED - Executables are truly portable!
```

## Integration with CI/CD

These scripts are automatically integrated into the GitHub Actions CI/CD pipeline:

- **Linux**: `.github/workflows/ci.yml` calls `examples/test-examples-linux-no-runtime.sh`
- **macOS**: `.github/workflows/ci.yml` calls `examples/test-examples-macos-no-runtime.sh`
- **Windows**: `.github/workflows/ci.yml` calls `examples/test-examples-windows-no-runtime.bat`

This ensures that every build automatically validates the true portability of generated executables across all supported platforms.

## Troubleshooting

### Common Issues

1. **"No executables found to test"**

   - Make sure to build the examples first using the appropriate build script
   - Check that `examples/output/` directory contains the expected executable files

2. **"Some runtimes are still available"**

   - This is often expected and acceptable - the fake executable approach should still work
   - The test continues as long as at least 3 out of 4 runtimes are successfully blocked

3. **Timeout issues**

   - Timeouts (exit code 124) are considered acceptable for testing purposes
   - They indicate the executable started successfully but may be a long-running service

4. **Permission errors**
   - Make sure the scripts have execute permissions (`chmod +x` on Unix systems)
   - On Windows, ensure the batch file can create temporary directories

### Debugging

Enable verbose output by examining the generated log files:

- `*_output.log`: Captured stdout from executable tests
- Check for error patterns in the output to diagnose specific failures

## Architecture

The portability testing system validates the core promise of UBuilder: that generated executables are truly portable and don't require host runtime installations. This is achieved through:

1. **Clean Environment Simulation**: Creating a runtime-free environment
2. **Comprehensive Testing**: Testing all generated executables systematically
3. **Detailed Reporting**: Providing actionable feedback on portability status
4. **Cross-Platform Support**: Consistent testing methodology across Linux, macOS, and Windows
5. **CI/CD Integration**: Automated validation in the build pipeline

This ensures that UBuilder's portability claims are continuously validated and any regressions in portability are caught early in the development process.
