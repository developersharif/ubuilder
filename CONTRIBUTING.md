# Contributing to UBuilder

Thank you for your interest in contributing to UBuilder! This document provides guidelines for contributing to the project.

## Development Setup

### Prerequisites

- CMake 3.16 or higher
- C/C++ compiler (GCC, Clang, or MSVC)
- Git

### Building from Source

1. Clone the repository:

   ```bash
   git clone <repository-url>
   cd ubuilder
   ```

2. Build the project:

   ```bash
   chmod +x build-system/build.sh
   ./build-system/build.sh
   ```

3. Run tests:
   ```bash
   cd build
   make test
   ```

## Project Structure

```
ubuilder/
├── src/                    # Core C/C++ source code
│   ├── core/              # Core UBuilder functionality
│   └── runtimes/          # Runtime-specific implementations
├── examples/              # Example applications
├── tests/                 # Test suite
├── build-system/          # Build automation scripts
└── docs/                  # Documentation
```

## Development Phases

The project is currently organized into phases as outlined in ProjectInstructions.md:

- **Phase 1**: Foundation (Current) - Basic embedding and Python runtime
- **Phase 2**: Multi-Platform Support
- **Phase 3**: Runtime Modularity
- **Phase 4**: Advanced Features
- **Phase 5**: Production Ready

## Coding Standards

- Use C11 for C code and C++17 for C++ code
- Follow consistent naming conventions
- Add comprehensive error handling
- Include unit tests for new functionality
- Document public APIs thoroughly

## Submitting Changes

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Ensure all tests pass
6. Submit a pull request

## Reporting Issues

Please use the issue tracker to report bugs or request features. Include:

- Detailed description of the issue
- Steps to reproduce
- Expected vs actual behavior
- Platform and version information

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
