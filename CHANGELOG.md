# Changelog

All notable changes to UBuilder will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.2.0] - 2026-05-19

### Added

- `--self-update` flag: downloads the latest release archive, smoke-tests
  the new binary, atomically replaces the running one.
- Update-available banner on each builder invocation. Cached 24h under
  `$XDG_CACHE_HOME/ubuilder/last-update-check`; refreshed via a detached
  curl so the build never blocks on the network. Silenced under
  `UBUILDER_NO_UPDATE_CHECK=1` or when stderr is not a TTY.
- `entry_point` from `ubuilder.json` is now honored at run time. Lets
  bundles use Laravel's `artisan`, Symfony's `bin/console`, or any other
  non-`main.php`/`index.php` entry. Implemented via a `.ubuilder.entry`
  marker file embedded as the first record.
- Argv passthrough: `./bundle serve --port=8000`, `./bundle migrate --seed`,
  `./bundle -v` now reach the embedded interpreter with argv intact. The
  trailer marker is the sole bundle/builder discriminator (previously any
  `-`-prefixed argv triggered the builder-mode fallback).
- Quiet output by default. A typical PHP build prints ~3 lines instead
  of ~20. `--verbose` / `-v` restores the old detail.

### Fixed

- PHP entry-point selector picked the first `.php` file alphabetically
  when `main.php` was absent, so projects using `index.php` ran a class
  file from `src/` instead of the real entry. Now prefers `main.php`
  then root-level `index.php` then any `.php` as fallback (or the
  `.ubuilder.entry` marker when present).
- FFI silently no-op'd in bundles. PHP 8+ defaults to
  `ffi.enable=preload`, which disables FFI in CLI mode. The generated
  `99-ubuilder-overrides.ini` now sets `ffi.enable=true`. Required for
  any FFI-based app (php-gui, php-tk, swoole, etc.) to function.

### Changed

- PHP bundles now copy the host's actual `php.ini` and `conf.d/`
  (following symlinks into `mods-available/`). The numeric prefixes on
  the conf.d files (`10-mysqlnd.ini`, `15-xml.ini`, `20-pdo_mysql.ini`,
  …) encode the correct load order, so we no longer guess at it. A
  `99-ubuilder-overrides.ini` is layered on top with the minimum forced
  settings (`ffi.enable=true`, `opcache.validate_timestamps=0`,
  `phar.readonly=0`).
- PHP bundles copy every `.so` from the host's `extension_dir` by
  default. `--exclude=ext-<name>` drops both the `.so` and the matching
  conf.d fragment.
- Launcher sets `PHP_INI_SCAN_DIR=<runtime_dir>/conf.d` so the bundle's
  PHP loads the copied fragments in priority order.
- Repository layout: `build-all.sh` and `create-release.sh` moved from
  the repo root into `scripts/`. Docs split into `docs/user/` and
  `docs/internals/`.

### Tests

- 184 unit assertions + 13 bundle cases + 4 tier-3 docker cases pass
  on Ubuntu 25.10.

## [2.1.1] - 2026-05-19

### Changed

- **PHP bundling policy: copy ALL host extensions by default; `--exclude` drops specific ones.** v2.1.0 only bundled extensions explicitly listed in `composer.json`'s `require.ext-*`, which broke real-world apps that load extensions at runtime without declaring them in composer (FFI-driven GUI apps, ondemand-loaded `mysqli`, etc.). New behavior:
  - `php_embed_runtime` scans the host's `extension_dir` and bundles every `.so` it finds.
  - `composer.json`'s `require.ext-*` is now informational: it drives the "missing ext-X — install php-X" diagnostic (preserves the existing user-friendly error) and feeds `--ignore-platform-req=ext-X` flags to `composer install` when those exts are excluded.
  - The `exclude` config / `--exclude` CLI flag drops specific extensions from both the bundle copy and the auto-load list. Excluded names are echoed per-line so users see the drop took effect.
  - Generated `php.ini` now emits **dependency-ordered** `extension=` / `zend_extension=` lines: `mysqlnd` before `mysqli`, `pdo` before `pdo_*`, `xml` before `dom`/`simplexml`, etc. Mirrors Debian's `conf.d/<priority>-<ext>.ini` ordering and silences the `undefined symbol: mysqlnd_global_stats`-style startup warnings.
  - Generated ini also sets `ffi.enable = true` (PHP 8+ defaults to `preload` which disables FFI in CLI mode). Required for FFI-based GUI / interop apps to actually function inside a standalone bundle. Harmless when `ext-ffi` isn't loaded.
  - Bundle size grows from ~6 MB to ~90 MB on a typical Debian host with ~38 extensions installed. Users who want lean bundles can `--exclude=<noisy-ext>` for the ones they don't need.

### Added

- **Default output path is now `dist/<name>`** when neither `--output` nor `"output"` in `ubuilder.json` is provided. `<name>` is derived from the project directory's basename (falls back to `app` if unresolvable). Mirrors the conventional `dist/` folder that most build tools use. User-supplied values still win.
- **Auto-exclude of the output tree from the bundle.** When the resolved output path is relative (e.g. `dist/app`), ubuilder now appends `<output-top-dir>/**` (e.g. `dist/**`) to the effective exclude list before walking the project. This fixes a long-standing pre-existing bug where the recursive app-embed step would walk into the very directory it was writing to, producing a multi-gigabyte self-referencing bundle that eventually failed with `Failed to compute payload SHA-256`. For root-level outputs (no path separator) the bare filename is excluded instead.
- Both defaults are silent on the zero-flag DX path; pass `--verbose` to see the `ubuilder: no output specified — defaulting to dist/X` and `ubuilder: auto-excluding output tree 'dist/**'` lines.

### Fixed

- **`copy_executable_template` now emits actionable errors.** Previously a stale `dist/executable` directory (left over from a crashed prior run, or a user-created dir mistakenly placed at the output path) produced an opaque `Failed to copy executable template / Resource extraction failed` chain with no hint what went wrong. The function now:
  - Detects when `output_path` exists and is a directory, naming the path and suggesting two concrete recoveries (`rm -rf <path>` or change `output` in `ubuilder.json`).
  - Auto-creates the parent directory of `output_path` when missing (so `"output": "dist/app"` works on a first-time build without manually `mkdir dist/`).
  - Includes `strerror(errno)` and the resolved path in all other `fopen` failure messages.
- Round-2 cross-platform CI fixes that landed between the failed `v2.1.0` tag attempt and the successful one are folded into this release for completeness:
  - `src/runtimes/install_cache.c` MSVC `S_ISDIR`/`S_ISREG` fallbacks.
  - `tests/test_platform_compat.c` MSVC `setenv`/`unsetenv` shims via `_putenv_s`.
  - Loosened `test_spawn_missing` assertion to handle libc-dependent posix_spawn semantics (glibc returns -1 before fork; macOS forks then child `_exit(127)`).
  - Tier-3 PHP image auto-selects by host libxml2 SONAME (`.16` → ubuntu:25.10, `.2` → ubuntu:24.04 for newer glibc coverage).

## [2.1.0] - 2026-05-19

### Added

- **`exclude` config field + repeatable `--exclude` CLI flag**, applied across all three runtimes:
  - **App-file globs** (`tests/**`, `*.md`, `[!a-z]*.tmp`, …) skipped during the recursive embed step for Python, PHP, and Node bundles. Supports `*`, `**`, `?`, `[abc]`, `[a-z]`, `[!abc]`, leading-`/` anchor, trailing-`/` dir-only, and backslash normalization.
  - **PHP `ext-<name>`** drops the entry from the composer-declared extension list before staging and passes `--ignore-platform-req=ext-<name>` to `composer install` so platform-req checks don't abort.
  - **Python wheel name** (PEP-503-normalized) filters `requirements.txt` line-by-line into a staged `.ubuilder.filtered-requirements.txt` before `pip install` runs.
  - **Node module name** removes the key from `dependencies` / `devDependencies` / `optionalDependencies` / `peerDependencies` of the staged `package.json` (re-emitted JSON, `unlink`-before-rewrite to break hardlink sharing with the user's source); staged `package-lock.json` is dropped if anything changed so `npm install` is used instead of `npm ci`.
  - CLI entries append to the config array; everything else still overrides.
  - Excluded deps invalidate the install-cache key, so cache hits stay correct.
- **Auto-write `ubuilder.json` on first build**: a successful build with no `ubuilder.json` writes one capturing `runtime`, `entry_point`, `name`, and `exclude`. Subsequent builds reuse it; no overwrite on later runs.
- **PHP M1-D (host-bits hermetic)**: synthesizes a runtime tree from the host's `/usr/bin/php` plus `composer.json`'s `require.ext-*` entries; runs `composer install --no-dev` in a staged project, with content-addressed install-cache for cache hits on the second build.
- **Glob matcher** (`src/core/glob_match.{c,h}`): a small portable subset of git's wildmatch.

### Tests

- New unit suites for glob matching (`tests/test_glob_match.c`, 39 assertions) and PHP builder internals (`tests/test_php_builder.c`, 26 assertions).
- New bundle cases: `python-with-exclude`, `nodejs-with-exclude`, `php-with-deps`, `php-missing-ext`, `php-exclude-ext`, `php-autoconfig`.
- New tier-3 (docker portability) PHP cases — bundle runs in a clean Ubuntu container that has only `php-cli`'s shared-lib transitives installed and `/usr/bin/php` removed, proving the bundle uses its own embedded `bin/php`.
- Total: 184 unit assertions + 13 end-to-end bundle cases + 4 tier-3 docker cases passing.

### Known limitations

- **PHP bundles are tied to the build host's libxml2 SONAME family.** The host's `/usr/bin/php` is hardlinked into the bundle and dynamically links `libxml2.so.<N>` (currently `.16` on Ubuntu 24.10+ / Debian Trixie+, `.2` on older glibc distros). A bundle built on Ubuntu 25.10 will fail with `libxml2.so.16: cannot open shared object file` when run on Debian 12 / Ubuntu 22.04. Truly hermetic ldd-bundling is future work; today, build on the same distro family as your deployment target. See `src/runtimes/php_builder.c` block comment.
- **PHP runtime on macOS is not supported in this release.** M1-D's synthetic-runtime path assumes Linux's `extension_dir` layout (`<prefix>/lib/php/<date>/`) and `apt`-shaped extension naming. Homebrew PHP's `Cellar/php/<ver>/lib/php/<datestamp>/` path + Mach-O extension files aren't handled yet. `examples/build-examples-macos.sh` skips the PHP example cleanly until M1-D-macos lands. Python and Node bundles on macOS are supported and hermetic.
- **Release script**: previous `create-release.sh` had two bugs that would have corrupted a release (`sed s/#define UBUILDER_VERSION.*/.../` clobbered MAJOR/MINOR/PATCH to the same string; CHANGELOG step always prepended a stub that pushed curated content down). Both fixed; the script now supports `--dry-run` and is idempotent on a pre-bumped tree.

### Fixed in this release (post-tag-cut)

- **MSVC compile errors** in `tests/test_{config,php_builder,platform_compat,sha256}.c` (unconditional `<unistd.h>` include) and `src/runtimes/runtime_embedder.c` (missing `S_ISDIR`/`S_ISREG` on MSVC). Tests now have a per-file Windows compat shim; `runtime_embedder.c` gains the standard bitfield macros under `#ifdef PLATFORM_WINDOWS`.
- **macOS auto-vendor failure** in `scripts/vendor-runtimes.sh` from `declare -A` (bash-4-only; macOS ships bash 3.2). Replaced with a space-padded string + glob-match membership test that's bash-3.2 compatible.
- **macOS hermetic runtimes**: added arm64 and x86_64 entries for Python (`python-build-standalone`) and Node (official `nodejs.org` darwin builds) so auto-vendor produces portable bundles on macOS instead of falling back to the non-portable Homebrew binary.

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
