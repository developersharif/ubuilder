# UBuilder — single-file hermetic executables for Python, PHP, and Node.js apps

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![CI](https://github.com/developersharif/ubuilder/workflows/UBuilder%20Cross-Platform%20CI/badge.svg)](https://github.com/developersharif/ubuilder/actions)
[![Linux](https://img.shields.io/badge/platform-Linux-blue.svg)]()
[![macOS](https://img.shields.io/badge/platform-macOS-blue.svg)]()
[![Windows](https://img.shields.io/badge/platform-Windows-blue.svg)]()

> Drop a Python, Node.js, or PHP app into a directory. Run `ubuilder`. Get one self-contained executable that runs on any machine of the same OS/arch — **no Python, no Node, no PHP, no `pip install`, no `npm install` required on the target.**

UBuilder bundles a vendored interpreter, your application, and all third-party dependencies into a single executable. The output is integrity-checked (SHA-256 over the payload) and extracts itself to a temp directory at launch. No global installs on the build machine either — the first run downloads pinned interpreter tarballs into a local cache.

---

## Quick start

```bash
# 1. cd into your app
cd my-python-app/        # contains main.py + (optional) requirements.txt

# 2. drop in a one-line manifest
echo '{"runtime":"python","entry_point":"main.py"}' > ubuilder.json

# 3. build
ubuilder

# 4. ship the result
./my-python-app          # runs on any Linux x86_64 with no Python installed
```

That's it. No flags, no environment setup. The first invocation auto-vendors the interpreter (~32 MB Python or ~30 MB Node, cached under `~/.cache/ubuilder/`). Subsequent builds reuse the cache.

### Three runtimes, one workflow

```bash
# Python — pip-installs requirements.txt into the staged interpreter
cd python-app && ubuilder

# Node.js — npm-installs package.json into a staged project tree
cd node-app && ubuilder

# PHP — currently host-fallback only; M1-D hermetic build is on the roadmap
cd php-app && ubuilder
```

---

## What you get

- **True zero-dep output.** The generated binary embeds the interpreter and every dependency. Strip `PATH` to `/usr/bin:/bin` and run the bundle in a `debian:12-slim` container — it works.
- **Integrity at the boundary.** Every bundle ends in a V4 trailer with SHA-256 over the payload. Tampered bundles refuse to run.
- **Apple-style sandbox discipline.** No `system()`, no `popen()`, no silent host fallback in hermetic mode. Every spawn goes through a structured-argv shim (`posix_spawnp` / `CreateProcess`). See [`docs/architecture/ARCHITECTURE_AUDIT.md`](docs/architecture/ARCHITECTURE_AUDIT.md).
- **Reproducible-ish.** Interpreter tarballs are pinned by SHA-256 in `scripts/vendor-runtimes.sh`. Lockfile support for user deps is on the roadmap (item #3).

---

## How it works

1. **Build mode** — `ubuilder` looks for `ubuilder.json` in `.` or `--project-dir`, picks a runtime builder, and writes a new executable laid out as:
   `[ubuilder launcher][runtime tree][app tree][V4 trailer with SHA-256]`.
2. **Launcher mode** — when the generated binary runs, it detects the trailer, verifies the SHA-256, extracts the payload to a temp dir, and execs the embedded interpreter against the embedded entry point.
3. **Hermeticity** — at build time the runtime comes from `~/.cache/ubuilder/runtimes/<rt>/`. If the cache is empty, `scripts/vendor-runtimes.sh` is auto-spawned to populate it from upstream (python-build-standalone, nodejs.org).
4. **User deps** — if your project has `requirements.txt` (Python) or `package.json` (Node), they're installed into a *staged* copy of the runtime/project before bundling. The shared cache is never polluted.

Deep dives:
- [`docs/architecture/ARCHITECTURE_AUDIT.md`](docs/architecture/ARCHITECTURE_AUDIT.md) — the engineering audit + the binding hermeticity principles.
- [`docs/architecture/M1_HERMETIC_INTERPRETERS.md`](docs/architecture/M1_HERMETIC_INTERPRETERS.md) — vendoring strategy and `--runtime-source` precedence.
- [`docs/architecture/M8_USER_DEPS.md`](docs/architecture/M8_USER_DEPS.md) — per-runtime dep-install mechanics.
- [`docs/architecture/CONFIG_FILE_SPEC.md`](docs/architecture/CONFIG_FILE_SPEC.md) — full `ubuilder.json` schema.
- [`docs/architecture/STATIC_LAUNCHER.md`](docs/architecture/STATIC_LAUNCHER.md) — `-DUBUILDER_STATIC=ON` for a fully static launcher (musl toolchain provided).

---

## CLI flags (all optional)

The zero-flag path is the default. You only need these for non-default cases:

| Flag | Purpose |
|------|---------|
| `--project-dir <path>` | Build from a directory other than `.` |
| `--runtime <python\|php\|node>` | Override manifest |
| `--output <path>` | Override output name (default: `<name>` from manifest) |
| `--entry-point <file>` | Override manifest's entry point |
| `--config <path>` | Use an explicit `ubuilder.json` |
| `--runtime-source <path>` | Use a specific vendored interpreter tree (skips cache discovery) |
| `--use-host-runtime` | Explicit opt-in to the host's interpreter — produces a **non-portable** bundle. Useful for fast local iteration |
| `--no-install-deps` | Skip `pip install` / `npm install` / `composer install` step |
| `--no-auto-vendor` | Don't auto-spawn `scripts/vendor-runtimes.sh` on cache miss |
| `--exclude <pat>` (repeatable) | Drop files/deps from the bundle: file glob (`tests/**`, `*.md`), PHP `ext-curl`, Python wheel name (`six`), or Node module name (`is-number`). Appends to `exclude` in `ubuilder.json`. |
| `--verbose` / `-v` | Show every spawned subprocess |

Run `ubuilder --help` for the canonical list.

---

## `ubuilder.json` manifest

```json
{
  "name": "my-app",
  "runtime": "python",
  "entry_point": "main.py",
  "exclude": ["tests/**", "*.md", "six"],
  "build": { "compression": true, "gui": false },
  "runtime_options": {
    "python": { "source": "...", "use_host": false, "no_install_deps": false }
  }
}
```

CLI flags override config keys (one exception: `--exclude` **appends** to `exclude`).
A successful build with no `ubuilder.json` writes one automatically — capturing the
resolved `runtime`, `entry_point`, `name`, and `exclude` so the next `ubuilder` run
needs no flags.

Full schema: [`docs/architecture/CONFIG_FILE_SPEC.md`](docs/architecture/CONFIG_FILE_SPEC.md).

---

## Building UBuilder from source

Most users should install a release binary. To build the tool itself:

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
./tests/test_ubuilder                       # 184/184 unit tests
../tests/bundle/run-bundle-tests.sh         # 13/13 end-to-end bundle cases
../tests/bundle/run-tier3.sh                # 4/4 Docker portability cases (py+node fully hermetic, php host-bits-hermetic)
```

Useful CMake options (top-level `CMakeLists.txt`):

- `-DBUILD_TESTS=OFF` — skip tests
- `-DENABLE_COMPRESSION=OFF` — disable ZLIB resource compression
- `-DUBUILDER_STATIC=ON -DCMAKE_TOOLCHAIN_FILE=../toolchains/musl-linux-x86_64.cmake` — static-musl launcher

---

## Project layout

```
ubuilder/
├── src/
│   ├── core/                 # ubuilder.{c,h}, platform_compat, json_mini, config, sha256
│   ├── runtimes/             # {python,php,nodejs}_{builder,runtime}.c + runtime_embedder
│   └── main.c                # CLI entry; routes builder vs launcher mode
├── scripts/
│   └── vendor-runtimes.sh    # SHA-pinned interpreter downloader
├── tests/
│   ├── *.c                   # unit tests (test_ubuilder)
│   └── bundle/               # end-to-end bundle + Tier-3 isolation harness
├── examples/{python,php,nodejs}/
└── docs/                     # architecture audit, M1, M8, config spec, roadmap
```

---

## Status

- ✅ Hermetic Python and Node.js (M1, M8) — vendor + dep install + Tier-3 Docker isolation passing.
- ✅ Integrity-checked V4 trailer (S3).
- ✅ Zero-flag DX via auto-vendor + auto-discover (S10).
- ⚠️ PHP runs in host-fallback mode only. Hermetic PHP (M1-D) is blocked on building static PHP via `static-php-cli` in CI. Tracked in [`docs/architecture/ROADMAP_NEXT.md`](docs/architecture/ROADMAP_NEXT.md) item #4.
- ⚠️ Only Linux x86_64 is vendored today. arm64 + macOS entries are roadmap item #5.

Full upcoming work: [`docs/architecture/ROADMAP_NEXT.md`](docs/architecture/ROADMAP_NEXT.md).

---

## License

MIT — see [LICENSE](LICENSE).
