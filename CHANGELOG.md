# Changelog

All notable changes to UBuilder will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.1] - 2025-06-25

### Fixed

- **PHP Extension Dependencies**: Resolved critical portability issue where PHP executables failed on different machines due to missing system extensions (like mbstring.so)
- **Extension Bundling**: PHP executables now bundle essential extensions (mbstring, json, ctype, iconv, fileinfo, curl, xml, dom) making them truly portable
- **Custom PHP Configuration**: Generated executables use custom php.ini that loads bundled extensions instead of system ones
- **Multi-byte String Support**: Unicode and multi-byte character handling now works correctly with embedded mbstring extension

### Added

- **Automatic Extension Detection**: UBuilder automatically detects and embeds essential PHP extensions from the system
- **Extension Extraction**: Runtime extraction of PHP extensions to isolated temporary directories
- **Custom php.ini Generation**: Automatic creation of php.ini files that configure embedded extensions

### Technical Details

- **PHP Executables**: Now include ~7 essential extensions adding ~13MB to executable size (total ~19MB)
- **Zero System Dependencies**: PHP executables no longer require any system PHP extensions
- **Extension Isolation**: Extensions are extracted to temporary directories with proper cleanup

## [2.0.0] - 2025-06-25

### Added

- **True Runtime Embedding**: Complete interpreter binaries are now embedded, not just launchers
- **Multi-File Project Support**: Full support for complex projects with imports/requires
- **Runtime-Specific Builders**: Modular architecture with dedicated builders for each runtime
- **Enhanced CLI**: Professional command-line interface with comprehensive validation
- **Cross-Platform Support**: Linux, macOS, and Windows compatibility
- **Zero Dependencies**: Generated executables require no runtime installation
- **Comprehensive Documentation**: Quick start, CLI reference, architecture, examples, and troubleshooting guides

### Changed

- **Architecture Overhaul**: Migrated from launcher-only to full runtime embedding
- **Size Increase**: Executables now 5-120MB (depending on runtime) vs previous 54KB launchers
- **Portability**: True portability - no system runtime dependencies required
- **Execution Model**: Runtime extraction and execution in isolated temporary directories

### Fixed

- **Multi-File Support**: Projects with require/import statements now work correctly
- **Embedded App Detection**: Improved detection logic to prevent CLI false positives
- **Working Directory**: Proper context handling for relative imports and file operations
- **Resource Extraction**: Robust extraction with error handling and cleanup

### Technical Details

- **PHP Runtime**: Embeds full PHP CLI interpreter (~6MB)
- **Python Runtime**: Embeds complete Python interpreter (~7MB)
- **Node.js Runtime**: Embeds Node.js binary and core modules (~120MB)
- **Build System**: CMake-based with automated runtime detection and embedding
- **Test Coverage**: 21 comprehensive tests with 100% pass rate

## [1.0.0] - 2025-06-24

### Added

- **Initial Release**: Basic launcher-only implementation
- **Core Framework**: C/C++ cross-platform foundation
- **Basic Runtime Support**: Python, PHP, Node.js launcher scripts
- **Build System**: CMake integration and automated builds
- **CLI Interface**: Command-line argument parsing
- **Project Structure**: Modular architecture foundation

### Known Limitations (Resolved in 2.0.0)

- Generated executables were only ~54KB (launcher scripts)
- Required target runtime to be installed on end-user systems
- Limited to single-file applications
- Dependency on system PATH for runtime detection

---

## Release Notes

### 2.0.0 - Major Release: True Runtime Embedding

This is a major milestone release that transforms UBuilder from a simple launcher generator to a complete application packaging solution. The 2.0.0 release delivers on the core promise of "truly portable, dependency-free executables."

**Upgrade Impact**:

- Generated executables are significantly larger but truly portable
- No breaking changes to CLI interface
- Existing projects work without modification
- Enhanced capabilities with multi-file project support

**Performance**:

- Build time: <45 seconds for typical applications
- Startup time: <2 seconds for embedded applications
- Memory overhead: Minimal (temporary extraction only)
- Disk space: Temporary files cleaned up automatically

**Compatibility**:

- **Linux**: Ubuntu 18.04+, CentOS 7+, Debian 10+
- **Windows**: Windows 10+ (cross-compilation ready)
- **macOS**: 10.15+ (cross-compilation ready)

For migration guidance and detailed feature documentation, see the [docs/](docs/) directory.
