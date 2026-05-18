# UBuilder Architecture Audit — Path to True Zero-Dependency Executables

**Date:** 2026-05-18
**Branch / commit:** `tests` @ `8b2adfa`
**Scope:** Linux x86_64 first-class; macOS / Windows / arm64 sketched.
**Tone:** Honest engineering review — gaps named, not softened.
**Binding principle (Apple-sandbox discipline):** Every change must increase UBuilder's hermeticity, integrity, and isolation; never decrease them. Concretely:

1. No `system()` / `popen()` / `/bin/sh` — not in the runtime path, not in the build path.
2. No silent fallback to host tools. Missing embedded resources must fail loudly.
3. Integrity at every boundary — SHA-256 (S3), eventually signatures (L5).
4. Hermetic builds — bundle output is a function of declared inputs, not "whatever was installed on the build host."
5. No host-environment leaks — overlay env explicitly, set cwd explicitly.
6. Temp-dir isolation via structured `pc_remove_tree`, never `system("rm -rf")`.
7. No `--no-verify`-style shortcuts on integrity checks.
8. Every hermeticity claim earns a test.

If a proposed change can't be justified against these, it doesn't ship.

---

## 0. TL;DR

UBuilder today is an **archive-and-extract launcher**, not a true single-file executable. The build step copies the `ubuilder` CLI binary, appends a blob containing one interpreter binary (plus, on Windows, some DLLs/stdlib), then appends the user's script files and a trailer. At run time the binary detects the blob, extracts it to `/tmp/ubuilder-<pid>/`, and calls `system(...)` against the extracted interpreter.

Concretely:

- The "embedded" Python/PHP/Node executable is whatever `/usr/bin/python3` (etc.) happened to be on the build host — not a statically linked, hermetic interpreter. It still depends on the build host's glibc, OpenSSL, libxml2, zlib, etc., to be present at the **target** host's matching ABI.
- There is no Python `Lib/`, no PHP extension set, and no Node `node_modules` linker embedded on Unix. Anything beyond a `print("hello")`-class script will fail at run time on a host with no Python install.
- The execution path calls `system(command)` (`src/core/ubuilder.c:526,653,655`) — a `/bin/sh` is therefore required on the target machine.
- The "no-runtime portability test" (`examples/test-examples-linux-no-runtime.sh`) only poisons `PATH`; it does not verify that the bundle is functionally hermetic.
- There are no end-to-end tests that build, bundle, and execute a fixture and assert its stdout. Unit tests cover string getters only.

**What "true 0-dependency" requires** (Linux x86_64): a statically linked C launcher + a hermetic interpreter tree (musl-built CPython / PHP / Node with their full stdlib) + an in-process extraction-and-exec path. Architectural changes below.

---

## 1. The goal vs. reality gap

| Stated claim | What the code actually does | Evidence |
|---|---|---|
| "True runtime embedding" | Appends one ELF (the host's `/usr/bin/python3`) to the launcher binary | `src/runtimes/python_builder.c:85-92`, `src/runtimes/runtime_embedder.c:239-276` |
| "Zero dependencies" | Bundle depends on target's glibc, libssl, libxml2, libz, `/bin/sh`, and (Python) the host's stdlib living somewhere on the system | `src/core/ubuilder.c:526` (`system()`); `python_builder.c:85-92` (binary-only embed on Unix) |
| "Cross-platform" | Each builder has two divergent code paths (Windows full-tree embed; Unix binary-only embed); macOS shares the Unix path and has zero macOS-specific embedding | `python_builder.c:80-92`, `php_builder.c`, `nodejs_builder.c:80-101` |
| "True embedding" (no extraction) | Extracts blob to `/tmp/ubuilder-<pid>/` every run, then `rm -rf` after | `src/core/ubuilder.c:1110-1116` |
| "Launcher" | Written as a literal C string into the build output but never actually compiled/used | `python_builder.c:232-241`, `php_builder.c:705-715`, `nodejs_builder.c:238-248` |
| Portability validated by CI | CI runs the bundle on the same host that has Python/PHP/Node installed; the "no-runtime" script only renames `PATH` | `.github/workflows/ci.yml:38-62`, `examples/test-examples-linux-no-runtime.sh:30-82` |

The `src/core/platform_compat.{c,h}` files exist but are **0 bytes** — the "platform abstraction layer" is currently a name only. Platform branching is open-coded with `#ifdef PLATFORM_WINDOWS` throughout `ubuilder.c` and the builders.

---

## 2. What the code actually does today (end-to-end)

### 2.1 Build (`ubuilder --project-dir=… --runtime=…`)

1. `ub_build_executable()` → `create_modular_executable()` (`src/core/ubuilder.c:202-219, 825-907`).
2. Looks up `ub_runtime_builder_t` for the requested runtime (`src/runtimes/runtime_builder.c`).
3. **Copies the running ubuilder CLI binary as a template** for the output (`ubuilder.c:852`), then opens it in append mode and writes:
   - Runtime payload via `builder->embed_runtime(fp)` — on Unix this is just `ub_embed_runtime_binary(/usr/bin/python3, fp)` (`runtime_embedder.c:239-276`), a fixed-buffer file copy.
   - Application files via `builder->embed_application(project_dir, fp)` — walks the project tree, writes `[name_len][name][file_size][bytes]` records.
   - A "launcher" placeholder via `builder->generate_launcher(fp)` — actually writes a C-string description, not executable code (`python_builder.c:232-241`).
   - A trailer with `data_start_offset` and a magic number (`ubuilder.c:894-907`).
4. The "embedded" interpreter is therefore **whatever interpreter the build host happens to have installed**. There is no toolchain that builds a hermetic interpreter. There is no version pinning.

### 2.2 Launcher detection (`ub_check_and_run_embedded_app`)

1. Re-opens `argv[0]` (`ubuilder.c:263-394`), scans backward for the magic-number trailer, reads `data_start_offset`.
2. If found → enters launcher mode and skips CLI parsing.

### 2.3 Run (`ub_run_modular_embedded_app`)

1. Picks a temp dir: `$TMPDIR` or `/tmp` on Unix, `%TEMP%` on Windows; appends `ubuilder-<pid>` (`ubuilder.c:396-410`).
2. Writes the runtime bytes back out to `<tmp>/runtime_binary`, application files alongside it.
3. Builds a command string and calls `system(...)` (`ubuilder.c:653-655`).
4. For PHP: `PHP_INI_SCAN_DIR= "<tmp>/runtime_binary" -c "<tmp>/php.ini" "<script>"` — relies on a `php.ini` that isn't actually written for Unix.
5. For Python: `PYTHONPATH=<tmp>/Lib:<tmp>/Lib/site-packages:$PYTHONPATH "<tmp>/runtime_binary" "<script>"` — those `Lib` directories don't exist on Unix because nothing embeds them.
6. After exit, `system("rm -rf <tmp>")` (`ubuilder.c:1110-1116`).

### 2.4 The silent-fallback footgun

`src/core/ubuilder.c:508-516` defines `ub_execute_script()` — used when there is **no** embedded runtime — which unconditionally builds the command `python3 "..."` / `php "..."` / `node "..."` and `system()`s it. Net effect: if extraction fails, or if a code path forgets to wire the embedded-runtime variant, the bundle silently uses the host's interpreter. No error, no log, no exit-non-zero. This makes the "0-dependency" claim impossible to test by black-box observation on a host that has the interpreter installed.

---

## 3. Critical gaps for true zero-dependency bundling

Numbered so we can reference them later.

**G1. Interpreter source is the build host.** Anyone running CI on Ubuntu 24.04 produces a bundle that requires the target's glibc to be ≥ that version. There is no build of a hermetic / portable interpreter.

**G2. Interpreter stdlib is not embedded on Unix.** Python without `Lib/`, PHP without its extension `.so`s, Node without its `node_modules` resolver wired correctly — none of these are real "interpreters." Only the executable byte is embedded on Linux/macOS.

**G3. ~~`system()` and `/bin/sh` dependence.~~** Closed. `grep -E 'system\(|popen\(' src/core/*.c src/runtimes/*.c` returns **zero matches outside comments**. The runtime execution path went through `pc_spawn_and_wait` + `pc_remove_tree` (S1); the build/extraction path now uses `pc_mkdir_p` (S4) and `pc_path_lookup` + `pc_spawn_capture` (S4/S5) instead of `system("mkdir …")` and `popen("which …" / "php-config …")`. UBuilder no longer depends on `/bin/sh` for any operation. (Host-tool probing is still non-hermetic by *intent* — true fix is M1 — but it no longer goes through a shell.)

**G4. No statically linked launcher.** The CLI itself is dynamically linked. Even if the interpreter were hermetic, the launcher's own glibc/libpthread/libdl deps still need to match the target.

**G5. Temp-dir-extraction model.** Extract-then-exec is fundamentally slower, leaks files on crash, requires `/tmp` to be writable and not `noexec`, and creates an attack surface (the extracted interpreter lives on disk briefly under a predictable PID-based path).

**G6. Platform abstraction is missing.** `platform_compat.{c,h}` are 0 bytes. Every platform difference is open-coded via `#ifdef PLATFORM_WINDOWS` in 1000-line files. Adding macOS-specific behavior, arm64, or musl support requires editing every call site.

**G7. ~~Argv re-quoting is broken.~~** Fixed in S1. Argv is now passed as a real array through `pc_spawn_and_wait`; no re-quoting, no 1024-byte cap. The bundle harness covers spaces and single quotes.

**G8. ~~No integrity checks.~~** Fixed in S3 — every bundle carries a SHA-256 of its payload in the V4 trailer; the launcher verifies before extraction and refuses to run on mismatch (loud error, no fallback). Still open under G8: no Ed25519 signatures yet (L5), and `runtime_embedder.c`'s `ub_resource_t.checksum` field is still unused (small follow-up: wire it into per-resource extraction).

**G9. No deduplicated runtime cache.** Each bundle ships its own copy of the interpreter (Python ≈ 8 MB, Node ≈ 80 MB). A user with 10 Node bundles ships 800 MB; first-run extraction cost compounds. The Windows Python path already half-acknowledges this with a `%LOCALAPPDATA%` cache (`runtime_embedder.c:678-730`) but the cache key is the bundle path, not a runtime hash — so two identical bundles still extract twice.

**G10. ~~`popen("php-config --extension-dir")` at build time.~~** Closed in S5 — the shell-out is gone (replaced with `pc_path_lookup` + `pc_spawn_capture` against a discovered `php-config`). The build remains non-hermetic in a deeper sense (it probes the host for PHP extension layout); M1 (bundled hermetic interpreters) is the actual fix.

**G11. Zero end-to-end test coverage.** `tests/test_core.c` covers `ub_get_version_string()` and `ub_parse_runtime()` only. There is no test that builds, bundles, and verifies execution. The "no-runtime" script poisons `PATH` but the bundle has `runtime_binary_path` as an absolute path to the extracted file — so PATH manipulation proves nothing about hermeticity.

**G12. Stale stubs.** `src/core/ubuilder.c.new`, `src/core/platform_compat.c`, `src/core/platform_compat.h`, and several root markdown files were 0-byte placeholders. (Most have now been moved into `docs/`; the source-tree stubs remain.)

**G13. GUI support flagged but unimplemented.** `--gui` is parsed (`src/main.c:31`) and stored in `ub_config_t.enable_gui` (`src/core/ubuilder.h:69`) but no code reads it. GUI on Linux additionally requires Xlib/Wayland linkage decisions and an Xvfb-vs-host-display strategy — neither addressed.

**G14. No config-file loader.** `examples/*/ubuilder.json` and the bundle fixtures all carry `ubuilder.json` files, but `grep -r ubuilder\.json src/` returns no parser. The convention is purely aspirational. For repeatedly built large codebases this forces a long `--project-dir=… --runtime=… --entry-point=… --output=…` invocation every time. Spec: `docs/architecture/CONFIG_FILE_SPEC.md`.

---

## 4. Recommended architectural changes

### 4.1 Short-term (correctness fixes, 1–2 weeks, no API churn)

| Action | Reason | Touch points |
|---|---|---|
| **S1. Replace `system()` with `posix_spawnp` / `posix_spawn` + `argv` array on Unix, `CreateProcessW` on Windows.** ✅ done (runtime path). | Removes `/bin/sh` dependency on the execution path (G3), fixes argv quoting (G7), enables proper exit-code propagation. Introduced `src/core/platform_compat.{c,h}` with `pc_spawn_and_wait` / `pc_remove_tree` / `pc_env_overlay` / `pc_env_free`. Rewrote `ub_execute_script_with_embedded_runtime` to build a real argv array (no re-quoting, no 1024-byte cap) and pass an environment overlay. Replaced `system("rm -rf …")` cleanup with `pc_remove_tree`. Bundle harness now passes args containing spaces and single quotes end-to-end. Build-time `system()`/`popen()` in `runtime_embedder.c` and `php_builder.c` remain — those are S4/S5. | `src/core/ubuilder.c`, `src/core/platform_compat.{c,h}` |
| **S2. Delete the silent host-runtime fallback in `ub_execute_script()`.** ✅ done. | Eliminates the "secretly using host interpreter" footgun (G3); failures should be loud. Removed `ub_execute_script`, `ub_execute_embedded_app`, `ub_run_legacy_embedded_app`, and the legacy v2-marker scan in `ub_check_and_run_embedded_app`. Modern modular bundles are the only execution path; missing/broken embedded runtimes now surface as `UB_ERROR_EXTRACTION_FAILED` / `UB_ERROR_EXECUTION_FAILED` instead of being papered over by `system("python3 ...")`. | `src/core/ubuilder.c` |
| **S3. Add SHA-256 over the payload, written into the trailer; verify on launch.** ✅ done. | G8. Bundles become tamper-evident; truncated/corrupt bundles fail fast with no fallback. | New `src/core/sha256.{c,h}` (public-domain FIPS 180-4 impl, ~150 LOC, NIST test vectors covered). V3 trailer bumped to V4 (`UBUILDER_MODULAR_V4_SHA256_MARKER`); old V3 bundles are intentionally rejected. Bundle harness now flips a byte in the file's payload region and asserts the bundle refuses to run with a clear `integrity check FAILED` diagnostic. |
| **S4. Populate `src/core/platform_compat.{c,h}` with structured spawn/fs helpers.** ✅ done (initial slice). | Collapses `system("mkdir …")` shell-outs (G3) and reduces the next batch of `#ifdef PLATFORM_WINDOWS` blocks (G6). Added `pc_mkdir_p`, `pc_path_lookup`, `pc_spawn_capture` alongside the S1 trio. Three `system("mkdir …")` sites in `runtime_embedder.c` and two `popen` sites (`execute_command` for `which`/`--version` discovery) are gone. Still open under S4: `pc_temp_root`, `pc_executable_path`, `pc_realpath` (these collapse more `#ifdef`s but don't fix G3). | `src/core/platform_compat.{c,h}`, `src/runtimes/runtime_embedder.c` |
| **S5. Remove `popen("php-config --extension-dir")`.** ✅ done. | Non-hermetic build (G10); breaks reproducibility. Replaced with `pc_path_lookup("php-config")` + `pc_spawn_capture`, plus a conventional fallback path with a verbose note. Real fix is M1 (bundled hermetic PHP), which makes this code irrelevant. | `src/runtimes/php_builder.c` |
| **S6. Drop the empty `*.new` and 0-byte source files.** | Stale (G12). | `src/core/ubuilder.c.new`, `src/core/platform_compat.{c,h}` — keep the latter, but populate them per S4. |
| **S7. Add real end-to-end bundle tests.** | G11. See `tests/bundle/` and §5. | Done — `tests/bundle/`. |
| **S8. Statically link the `ubuilder` launcher with musl.** | G4. Build via `zig cc -target x86_64-linux-musl` or `musl-gcc`. Static launcher is < 200 KB. | `CMakeLists.txt`, new `toolchains/musl-linux-x86_64.cmake`. |
| **S9. Implement `ubuilder.json` config-file loader.** ✅ done. | Repeated CLI invocations on a large project are user-hostile; `ubuilder.json` already exists as convention but nothing parses it (G14). Spec: `docs/architecture/CONFIG_FILE_SPEC.md`. | `src/core/json_mini.{c,h}` + `src/core/config.{c,h}`; `src/main.c` integration; `tests/test_config.c`; bundle harness now exercises discovery. |

### 4.2 Medium-term (real embedding, 1–2 months)

| Action | Why | How |
|---|---|---|
| **M1. Vendor or build hermetic interpreters per platform-arch.** | G1, G2. The build host's `/usr/bin/python3` is not portable. | Maintain `vendor/runtimes/<runtime>-<version>-<platform>-<arch>.tar.zst` — built once via a CI matrix using musl (Linux), `--enable-shared=no --without-system-libs` (PHP), `nodejs` static builds. Cache under `~/.cache/ubuilder/runtimes/`. UBuilder downloads on first use or accepts `--runtime-bundle=<path>`. |
| **M2. Bundle the entire interpreter tree, not just the binary.** | G2. | Use the existing Windows path generalization in `runtime_embedder.c` for all OSes. Walk the runtime tree, embed each file as a `[path_len][path][size][bytes]` record. |
| **M3. Switch payload format to a versioned container.** | G8. Allows future-proofing (compression, encryption, multiple entry points, manifests). | Define `UB1` container: `[magic 'UB01'][header_len][CBOR header][payload][SHA256][footer magic]`. CBOR keeps it tiny without a dependency; or fall back to a 64-byte fixed header. |
| **M4. Add zstd compression for the payload.** | Bundle size (G9). zstd is BSD-licensed, ~3× faster decompression than zlib at higher ratios. | Replace ZLIB optionality with zstd vendored as a single amalgamation file. |
| **M5. Content-addressed runtime cache.** | G9, G5. | Extract under `~/.cache/ubuilder/runtimes/<sha256>/`. If the directory already exists, skip extraction. Bundles sharing a runtime hash share a cache entry. |
| **M6. Replace the placeholder "launcher" generation with a real two-binary scheme**: a tiny statically-linked stage-1 launcher (the current `ubuilder` CLI) and a stage-2 interpreter-specific entry point. | G3, G5. | Remove `generate_launcher` entirely; the stage-1 binary is the launcher. |
| **M7. Honest `--gui` semantics or remove the flag.** | G13. | On Linux: detect `$DISPLAY` / `$WAYLAND_DISPLAY`; on macOS / Windows it's a no-op for CLI apps. Or remove the flag until there's a plan. |

### 4.3 Long-term (truly hermetic, 3–6 months)

| Action | Why | How |
|---|---|---|
| **L1. In-process extraction via memfd / `fexecve` on Linux.** | G5. Eliminates on-disk extraction surface for the interpreter binary itself. | `memfd_create` + `fexecve` on Linux ≥ 3.17. macOS: `posix_spawn` from a `/tmp` file is the only option without notarization signing of dynamic mounts. Windows: load the interpreter DLL with `LdrLoadDll` from memory via the `MemoryModule` technique — known but legally grey; alternative is per-user `%LOCALAPPDATA%` cache (already exists for Python on Windows). |
| **L2. AppImage-style squashfs payload.** | Mount-once, no full extraction; works with `noexec` `/tmp` via `fuse2fs`. | Vendor `squashfuse` or use kernel-side mount with `CAP_SYS_ADMIN` (root only — not viable). For non-root, use FUSE; fallback to extract. |
| **L3. Multi-arch fat binary.** | Currently the bundle is single-arch. | Header lists `[arch][offset][size]` records; loader picks at run time. Mac-style "Universal" binary applied across OSes. |
| **L4. PHP/Python C-API embedding.** | Replace `system(interpreter)` with `Py_InitializeEx()` / `php_embed_init()`. The interpreter becomes a library, not a subprocess. | Highest engineering cost; biggest payoff (no extraction at all for the interpreter; stdlib still extracts). |
| **L5. Signed bundles.** | Distribution trust. | Ed25519 over the trailer; optional `--sign-key=` flag; verifier in stage-1. |

---

## 5. Specific module-level recommendations

### `src/core/ubuilder.c` (1183 lines — too large)

Split into:
- `ubuilder.c` — public API (`ub_init`, `ub_cleanup`, `ub_build_executable`, dispatch only).
- `bundle_writer.c` — `create_modular_executable()` and helpers (currently lines ~825–907 plus embed helpers).
- `bundle_reader.c` — `ub_check_and_run_embedded_app()`, trailer scanning, magic detection.
- `extraction.c` — temp dir, write-out, cleanup.
- `execution.c` — process spawning (post-S1, posix_spawn / CreateProcess only).

### `src/runtimes/*_builder.c`

The Windows and Unix paths duplicate logic (file walking, FIND/opendir, write-record). Extract a shared `runtime_tree_walker` that calls a `for_each_file(path, callback, ctx)` and have each builder supply only the *root path resolution* (find-the-interpreter) and *file filter* (which extensions to embed).

### `src/runtimes/runtime_embedder.c` (822 lines)

Same shape — too large and mixes "find binary on host," "copy bytes," and "extract bytes at runtime" responsibilities. Split into `embedder_collect.c` (build-time) and `embedder_extract.c` (run-time). Run-time path **must not link** any host-discovery code (it can't be called and inflates the launcher binary).

### `src/core/platform_compat.{c,h}`

Currently 0 bytes. Populate per S4. Suggested header skeleton:

```c
// platform_compat.h
typedef struct { int fd; const char* path; } pc_tempfile_t;

const char* pc_temp_root(void);                 // $TMPDIR or /tmp or %TEMP%
int pc_mkdir_p(const char* path);               // -1/errno on fail
int pc_remove_tree(const char* path);           // recursive rm
int pc_executable_path(char* out, size_t n);    // /proc/self/exe or _NSGetExecutablePath or GetModuleFileNameW
int pc_spawn_and_wait(const char* exe, char* const argv[], char* const envp[]);  // returns child exit code
ssize_t pc_read_full(int fd, void* buf, size_t n);
ssize_t pc_write_full(int fd, const void* buf, size_t n);
```

### `src/main.c` argument parsing

The Windows hand-rolled branch (`src/main.c:51-…`) and the POSIX `getopt_long` branch will drift. Replace both with a vendored single-file arg parser (`docopt.c`, `argtable3`, or a 150-line hand-rolled portable one). One source of truth.

### `tests/`

Current tests are smoke checks for getters. Add:
- `test_bundle_io.c` — write a trailer, read it back; corrupt a byte, expect failure; truncated file, expect failure.
- `test_arg_quoting.c` — round-trip args containing space / quote / `$` / backslash through the spawn API.
- `test_platform_compat.c` — temp_root exists, mkdir_p creates nested, remove_tree cleans, executable_path resolves to a real file.

End-to-end coverage lives in `tests/bundle/` — see §6.

---

## 6. Testing strategy

Three tiers:

**Tier 1 — Unit (CMake/ctest).** Existing `tests/test_ubuilder`. Add the three new suites above.

**Tier 2 — Bundle integration (`tests/bundle/`).** For each runtime: build `ubuilder` → bundle a fixture app with deterministic stdout → run the bundle from a clean directory → assert exit code + exact stdout. Driver is `tests/bundle/run-bundle-tests.sh`; fixtures live in `tests/bundle/fixtures/<runtime>/`. Designed to run identically on dev laptop and in CI. **This is the first test that actually validates the product's core claim.**

**Tier 3 — Hermeticity (CI only, optional locally).** Run the same fixture in a Docker container that has no host interpreter installed. Today the harness skeleton is laid down (PATH poisoning re-implemented as a fallback for non-Docker dev runs) but the real `alpine`-based clean-room job is medium-term work after M1 (hermetic interpreters) lands — until then the bundles legitimately can't pass Tier 3 because their embedded interpreters depend on host libc.

Order of work: **land Tier 2 now** (it's the most useful gate); land Tier 3 alongside M1.

---

## 7. Suggested phasing

| Phase | Weeks | Deliverables |
|---|---|---|
| 0 — Audit + harness | This PR | This doc; Tier 2 harness; CI integration; remove silent fallback (S2). |
| 1 — Correctness | 1–2 | S1, S3, S4, S5, S6, S8. |
| 2 — Real embedding | 3–6 | M1, M2, M3, M4, M5. Tier 3 passes. |
| 3 — Polish | 7–10 | M6, M7, L5, multi-arch CI matrix (x86_64, arm64). |
| 4 — Truly hermetic | 11–24 | L1, L2, L4. |

---

## 8. Open questions for the team

1. **Interpreter sourcing for M1.** Build from source per release, or vendor pre-built (e.g., `indygreg/python-build-standalone`, `astral-sh/python-build-standalone`, `nodejs/unofficial-builds` musl Node, `php-static`)? Pre-built is faster to ship; building from source is more auditable.
2. **License surface.** Embedding PHP (PHP License), Python (PSF), and Node (MIT + V8 BSD) inside a redistributable executable has notice obligations. Need a `--license` flag and a `THIRD_PARTY_LICENSES` blob in the bundle.
3. **Single binary vs. binary + sidecar.** True single-file is harder; binary + auto-extracted sidecar dir under `~/.cache/ubuilder/` (the M5 model) is easier and still "feels" like 0-dep to end users. Pick one and document.
4. **Versioning across runtime + bundle.** Should a bundle built with ubuilder 2.0 + Python 3.12 keep working when the user upgrades ubuilder to 2.1? Yes (the bundle is self-contained), but the cache layout (M5) must be content-addressed to allow coexistence.
5. **GUI scope.** Drop `--gui` until there's a plan, or define it as "passes `$DISPLAY` through and trusts the embedded interpreter has the needed bindings"? The latter is honest and small.
