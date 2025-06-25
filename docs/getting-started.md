# Getting Started with UBuilder

UBuilder is a cross-platform solution that packages applications written in various programming languages as single, dependency-free executables.

## Installation

### From Source

1. **Prerequisites**

   - CMake 3.16 or higher
   - C/C++ compiler (GCC, Clang, or MSVC)
   - Git

2. **Clone and Build**

   ```bash
   git clone <repository-url>
   cd ubuilder
   chmod +x build-system/build.sh
   ./build-system/build.sh
   ```

3. **Verify Installation**
   ```bash
   ./dist/bin/ubuilder --version
   ./dist/bin/ubuilder --help
   ```

## Quick Start

### Building Your First Application

1. **Create a simple Python application**

   ```python
   # hello.py
   print("Hello from UBuilder!")
   ```

2. **Build with UBuilder**

   ```bash
   ./dist/bin/ubuilder --project-dir=./my-project \
                       --runtime=python \
                       --entry-point=hello.py \
                       --output=hello-app
   ```

3. **Run the executable**
   ```bash
   ./hello-app
   ```

### Supported Runtimes

- **Python**: Package Python scripts and applications
- **PHP**: Package PHP CLI applications
- **Node.js**: Package Node.js applications

### Basic Usage

```bash
ubuilder --project-dir=<path> --runtime=<runtime> --output=<name> [options]
```

**Required Arguments:**

- `--project-dir`: Source project directory
- `--runtime`: Runtime type (python, php, node)
- `--output`: Output executable name

**Optional Arguments:**

- `--entry-point`: Application entry point file
- `--gui`: Enable GUI support
- `--verbose`: Enable verbose output

## Examples

See the [examples](../examples/) directory for complete example applications:

- [Python Hello World](../examples/python-hello/)
- [PHP Hello World](../examples/php-hello/)
- [Node.js Hello World](../examples/node-hello/)

## Next Steps

- Read the [Architecture Guide](architecture.md) to understand how UBuilder works
- Check out the [API Reference](api-reference.md) for detailed function documentation
- Explore [Runtime-Specific Guides](runtime-guide.md) for advanced usage
