# UBuilder Documentation

This directory contains comprehensive documentation for the Universal Executable Framework (UBuilder).

## Documentation Structure

### Core docs
- [Getting Started](getting-started.md) - Quick start guide and installation
- [Quick Start](quick-start.md) - Minimal walkthrough
- [CLI Reference](cli-reference.md) - Command-line interface reference
- [Architecture](architecture.md) - Technical architecture and design
- [Examples](examples.md) - Example applications and tutorials
- [Releases](releases.md) - Release notes
- [Troubleshooting](troubleshooting.md) - Common issues and solutions

### Architecture
- [Architecture Audit](architecture/ARCHITECTURE_AUDIT.md) — honest engineering review of the path to true 0-dependency executables (gaps, refactors, phasing)
- [Config File Spec (`ubuilder.json`)](architecture/CONFIG_FILE_SPEC.md) — schema, discovery, and CLI/config precedence for the planned config-file loader
- [Static Launcher (S8)](architecture/STATIC_LAUNCHER.md) — how to build the launcher with `-DUBUILDER_STATIC=ON` so it has zero shared-library deps, and the musl toolchain for hermetic distribution builds
- [Hermetic Interpreters (M1)](architecture/M1_HERMETIC_INTERPRETERS.md) — vendoring strategy, bundle-format change, `--runtime-source` plumbing, and Tier-3 plan
- [User Dependency Install (M8)](architecture/M8_USER_DEPS.md) — staging the runtime, pip-installing `requirements.txt` into a hermetic tree without polluting the shared cache

### Guides
- [Project Instructions](guides/project-instructions.md)
- [Debug Usage](guides/debug-usage.md)
- [Bundle Test Harness](../tests/bundle/README.md) — end-to-end validation (build → bundle → run → assert)

### Reports & status notes
- [Project Completion](reports/project-completion.md)
- [Project Status](reports/project-status.md)
- [Final Status](reports/final-status.md)
- [Portability Fixes Applied](reports/portability-fixes-applied.md)
- [Library Embedding Success](reports/library-embedding-success.md)
- [Library Embedding Success Notes](reports/library-embedding-success-notes.md)
- [CPU Architecture Compatibility](reports/cpu-architecture-compatibility.md)
- [Docker Compatibility](reports/docker-compatibility.md)
- [Runtime Analysis](reports/runtime-analysis.md)

## Quick Links

- [Main README](../README.md)
- [Changelog](../CHANGELOG.md)
- [Contributing Guide](../CONTRIBUTING.md)
- [License](../LICENSE)
