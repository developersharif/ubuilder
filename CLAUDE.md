# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

UBuilder is a C11/C++17 framework that packages Python, PHP, and Node.js applications into single dependency-free executables. The `ubuilder` CLI takes a project directory plus a runtime selector and emits a self-contained binary that embeds the interpreter and the application; at startup the generated binary extracts itself to a temp directory and execs the embedded entry point.

## Build & test

CMake (>= 3.16) is the source of truth. The shell wrappers all funnel into it.

```bash
# Out-of-tree configure + build (Release, parallel)
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j

# Or use the prebuilt scripts:
./build-system/build.sh          # configures, builds, installs to ./dist/
./build-all.sh                   # platform-detecting wrapper that runs examples/build-examples.sh

# Tests (ctest target wired up via tests/CMakeLists.txt)
cd build && ctest --output-on-failure
# Or run the test binary directly to see the printf-based "✓/✗" output:
./build/tests/test_ubuilder
```

There is no single-test selector — `test_ubuilder` is one executable that internally calls `test_core_functions()` and `test_runtime_manager()`. To run a subset, edit `tests/test_main.c` or temporarily compile only the suite you want.

Useful CMake options (defined in top-level `CMakeLists.txt`):
- `-DBUILD_TESTS=OFF` — skip the test target
- `-DENABLE_COMPRESSION=OFF` — disable ZLIB-backed resource compression (auto-disables if ZLIB isn't found)
- `-DENABLE_SIGNING=ON` — placeholder for code-signing

Cross-platform notes:
- Windows MSVC: `_CRT_SECURE_NO_WARNINGS` and several `#pragma warning(disable: …)` are set in `src/core/ubuilder.h`; do not remove them when touching that header.
- Argument parsing in `src/main.c` has two branches — `getopt`-based for POSIX, hand-rolled for `PLATFORM_WINDOWS`. Keep them in sync when adding flags.

## Example projects

`examples/{python,php,nodejs}/` each contain a small program plus a `ubuilder.json` manifest. The `examples/build-examples*.{sh,bat,ps1}` scripts dispatch to the platform-specific variant; outputs land in `examples/output/` (gitignored).

```bash
# After building ubuilder, package an example:
./build/src/ubuilder --project-dir=./examples/python --runtime=python --output=hello
./hello
```

CLI flags (see `src/main.c:23` for the canonical list): `--project-dir/-p`, `--runtime/-r {python|php|node}`, `--output/-o`, `--entry-point/-e`, `--gui/-g`, `--verbose/-v`, `--help/-h`, `--version/-V`.

## Architecture

Two layers live in one binary, distinguished at startup by `ub_check_and_run_embedded_app()`:

1. **Builder mode** (no embedded payload appended): parse CLI args → pick a runtime builder → write a new executable that concatenates `[ubuilder bootstrap][runtime payload][app payload][trailer]`.
2. **Launcher mode** (payload present in this binary): extract payload to a temp dir, locate the entry point, exec the embedded interpreter against it, then clean up.

### Module layout

- `src/core/` — `ubuilder.{c,h}` is the public C ABI (see `src/core/ubuilder.h:42` for `ub_result_t` error codes and `src/core/ubuilder.h:62` for `ub_config_t`). `platform_compat.{c,h}` abstracts path/process/extraction differences across Linux/macOS/Windows.
- `src/runtimes/` — Two parallel interfaces:
  - **Builder side** (`runtime_builder.h`): one `ub_runtime_builder_t` vtable per language (`python_builder`, `php_builder`, `nodejs_builder`) with `validate_project` / `embed_runtime` / `embed_application` / `generate_launcher` hooks. This is what `--build` time uses.
  - **Launcher side** (`runtime_manager.h`): `ub_runtime_config_t` carries the embedded runtime resources (`ub_resource_t*`) and is consumed by `runtime_extract` / `runtime_execute` at run time.
  - Each runtime has two files: `<lang>_builder.c` (builder vtable instance) and `<lang>_runtime.c` (launcher-side init).
  - `runtime_embedder.{c,h}` is the shared payload-blob format used by both sides.
- `src/main.c` — CLI entry; calls `ub_check_and_run_embedded_app()` first to support the launcher case before falling through to argument parsing.

**Adding a new runtime** means: implement a `<lang>_builder.c` providing a `ub_runtime_builder_t` instance, implement a `<lang>_runtime.c` providing a `<lang>_runtime_init()` matching `runtime_manager.h:30`, add both `.c` files to `src/CMakeLists.txt`, and add the enum entry to `ub_runtime_type_t` (`src/core/ubuilder.h:55`) plus parsing in `ub_parse_runtime`.

### Build graph

- `ubuilder_core` (static library) bundles core + all runtimes.
- `ubuilder` (executable) is just `main.c` linked against `ubuilder_core`.
- Platform link deps live in `src/CMakeLists.txt`: `kernel32 user32` on Windows, `CoreFoundation` framework on macOS, `pthread dl` on Linux.
- ZLIB is optional and gated by `HAVE_ZLIB`.

## Code review graph (MCP)

This repo has a code-review-graph index. Per `~/CLAUDE.md`, prefer `semantic_search_nodes` / `query_graph` / `get_impact_radius` / `detect_changes` over Grep when exploring or reviewing. The graph auto-updates via hooks; fall back to file scanning only when the graph doesn't cover what you need.

## Docs

All long-form docs live under `docs/` — guides in `docs/guides/`, status/historical write-ups in `docs/reports/`. Only `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, and `LICENSE` stay at the root.
