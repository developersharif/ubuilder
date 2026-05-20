# UBuilder Config File Spec — `ubuilder.json`

**Status:** Implemented (v1). Core schema + discovery + CLI/config precedence shipped.
Honored keys: `schema_version`, `runtime`, `entry_point`, `output`, `name` (as output default),
`verbose`, `gui`, `compression`, `console`, `exclude`, `runtime_options.<rt>.{source,use_host}`.
Parsed-but-not-yet-honored: `include`, `build.*`.
Auto-write: a successful build with no `ubuilder.json` in `--project-dir` writes one capturing
the resolved runtime, entry_point, name, and exclude list. Subsequent builds reuse it.
**Companion to:** `ARCHITECTURE_AUDIT.md` (this is the deliverable for §4.1 "missing" — config-file ergonomics).
**Motivation:** Repeatedly typing
`ubuilder --project-dir=./myapp --runtime=python --entry-point=src/cli.py --output=dist/myapp --verbose`
is painful for large or frequently rebuilt projects. A checked-in `ubuilder.json` makes the build reproducible, IDE-friendly, and trivial to invoke (`ubuilder` with no args from the project root).

## 0. Current state

- `examples/{python,php,nodejs}/ubuilder.json` and `tests/bundle/fixtures/*/ubuilder.json` exist and use a consistent shape.
- **No code reads them.** `src/main.c:46` is the only argument source; `grep -r ubuilder\.json src/` returns zero parser hits.
- The convention is therefore aspirational. This spec promotes it to behavior.

## 1. Design goals (ranked)

1. **Zero-arg build from the project root.** `cd my-app && ubuilder` Just Works if `ubuilder.json` is present.
2. **CLI flags always win.** Config provides defaults; flags override. This keeps one-off builds and CI overrides easy.
3. **Schema-first, validated.** Unknown keys are a warning (forward-compat); type mismatches are a hard error with a useful message and the offending line/column.
4. **No new runtime dependency.** Vendor a single-file JSON parser (jsmn or similar). No libjson-c, no system requirements.
5. **Honest minimum.** Ship the smallest schema that's useful. Don't pre-design fields for features that don't exist (no `signing`, no `multi_arch` until the code supports them — the audit's L-tier).
6. **Future-proof container.** Reserve `schema_version` so we can evolve without breaking old configs.

Explicit non-goals: no environment-variable interpolation in v1, no `extends`/inheritance, no per-environment overrides (a/b/staging/prod), no JSON5/comments. Keep it boring.

## 2. File location & discovery

Resolution order (first match wins):

1. `--config <path>` CLI flag — explicit, absolute or relative to CWD.
2. `ubuilder.json` in `--project-dir` if `--project-dir` was passed.
3. `ubuilder.json` in current working directory.
4. No config — pure CLI mode, exactly as today.

**No ancestor search.** Walking up the tree is a common source of "why is this picking up the wrong config?" bugs. If the user wants a non-CWD config, they pass `--config`.

If `--config` is given and the file is missing, that's an error (exit non-zero). If discovery via #2 or #3 finds nothing, that's not an error — fall through to #4.

## 3. Schema (v1)

```json
{
  "schema_version": 1,

  "name": "my-app",
  "runtime": "python",
  "entry_point": "src/cli.py",
  "output": "dist/my-app",

  "include": ["src/**/*.py", "data/**/*.json"],
  "exclude": ["**/__pycache__/**", "**/*.test.py"],

  "verbose": false,
  "console": true,
  "compression": true,

  "runtime_options": {
    "python": {
      "version": "3.12",
      "site_packages": ["./vendor"]
    },
    "php": {
      "version": "8.3",
      "extensions": ["mbstring", "json"],
      "ini": "./php.ini"
    },
    "node": {
      "version": "20",
      "node_modules": "./node_modules"
    }
  },

  "build": {
    "clean": false,
    "parallel": true,
    "cache_dir": "~/.cache/ubuilder"
  }
}
```

### 3.1 Required keys

Exactly two:

- `runtime` — `"python" | "php" | "node"`. No default.
- `entry_point` — relative to the directory containing `ubuilder.json`. No default.

Everything else has a default (see §3.3).

### 3.2 Key semantics

| Key | Type | Default | Notes |
|---|---|---|---|
| `schema_version` | int | `1` | If absent, treated as `1`. Future bumps require a migration note. |
| `name` | string | basename of project dir | Used as the default `output` filename if `output` is unset. |
| `runtime` | enum | required | Matches existing `ub_parse_runtime()` values. |
| `entry_point` | string | required | Resolved relative to the config file's dir. |
| `output` | string | `./<name>` (or `./<name>.exe` on Windows) | Absolute or relative to CWD (not config dir — match shell intuition). |
| `include` | string[] | `["**/*"]` | Glob patterns matched relative to the config file's dir. Parsed but not honored yet — every file in the project dir is embedded unless dropped by `exclude`. |
| `exclude` | string[] | `[]` | Three categories, all applied: (a) file/dir glob patterns skipped during app embed (`*`, `**`, `?`, `[abc]`, `[a-z]`, `[!abc]`, leading-`/` anchor, trailing-`/` dir-only); (b) PHP `ext-<name>` (or bare `<name>`) drops the entry from the composer-declared extension list AND passes `--ignore-platform-req=ext-<name>` to `composer install`; (c) Python wheel name (PEP-503-normalized) filters `requirements.txt` before pip runs; (d) Node module name removes the key from `dependencies`/`devDependencies`/`optionalDependencies`/`peerDependencies` of the staged `package.json` (lockfile is dropped if anything changed). Excluded deps invalidate the install-cache key, so cache hits stay correct across exclude changes. |
| `verbose` | bool | `false` | |
| `console` | bool | `false` | **Windows only.** When `false` (default), the output `.exe` runs without a console window (PE subsystem set to GUI). Set to `true` for CLI tools, scripts, or anything that writes to stdout/stderr. Ignored on Linux and macOS. |
| `compression` | bool | `true` if `ENABLE_COMPRESSION` was on at build time, else `false` | |
| `runtime_options.<runtime>` | object | `{}` | Per-runtime nested map. Only the matching subkey is read. |
| `build.clean` | bool | `false` | If true, wipe `output` parent dir before building. |
| `build.parallel` | bool | `true` | Allow concurrent file embedding (no-op until parallel embedding lands). |
| `build.cache_dir` | string | `$XDG_CACHE_HOME/ubuilder` or `~/.cache/ubuilder` on Unix; `%LOCALAPPDATA%\ubuilder` on Windows | Aligns with audit §4.2 M5. |

### 3.3 Validation rules

- Unknown top-level keys: warning to stderr, build continues.
- Unknown keys inside `runtime_options.<runtime>`: warning.
- Type mismatch: hard error with `file:line:col` and the expected type.
- `runtime_options` for a runtime *other* than the configured `runtime` is silently allowed (lets one config file serve multiple targets, or carry historical entries).
- Glob syntax is a portable subset — `*`, `**`, `?`, character classes `[abc]`. No brace expansion (`{a,b}`) in v1; keep the matcher small.

### 3.4 CLI ↔ config precedence

For every CLI flag that has a config equivalent: **CLI wins**. Specifically:

| CLI flag | Config key |
|---|---|
| `--project-dir`, `-p` | (the dir containing `ubuilder.json`) |
| `--runtime`, `-r` | `runtime` |
| `--output`, `-o` | `output` |
| `--entry-point`, `-e` | `entry_point` |
| `--gui`, `-g` | `gui` |
| `--verbose`, `-v` | `verbose` |
| (no CLI flag — config only) | `console` |
| `--runtime-source <path>` | `runtime_options.<rt>.source` |
| `--use-host-runtime` | `runtime_options.<rt>.use_host` |
| `--no-install-deps` | (no config equivalent yet) |
| `--no-auto-vendor` | (no config equivalent yet) |
| `--exclude <pat>` (repeatable) | `exclude` (CLI entries **append** to the config array; the only "merging" exception — every other flag overrides) |
| `--config <path>` | (selects the file) |

There is no CLI form for `include`/`runtime_options.<rt>.{version,extensions,…}` in v1 — those are config-only.

## 4. Implementation plan

### 4.1 Parser

**Implementation:** hand-rolled recursive-descent parser at `src/core/json_mini.{c,h}` (~300 LOC, owned in-tree). It produces a typed tree (object / array / string / number / bool / null) so the config layer above it is straightforward.

Rationale for not vendoring `jsmn`: the parser surface is small enough that owning ~300 lines of straightforward C beats the "where did we get this, what version, did we patch it" maintenance question. The owned parser has accurate `line:col` errors, no allocations inside the parser hot path (only at value construction), and zero external dependencies.

Rejected alternatives considered: `jsmn` (token-array API, but we'd still wrap it in ~150 LOC of value extraction), `cJSON` (heavier, allocates), `parson` (heavier), `yajl` (system dep). If the schema ever grows beyond what this parser covers cleanly (e.g., we want streaming for huge configs, or true JSON5/comments), revisit.

### 4.2 New files

```
src/core/
  json_mini.{c,h}    # hand-rolled JSON tree parser
  config.{c,h}       # config loader: ub_config_load / ub_config_apply / ub_config_free
tests/
  test_config.c      # parser + config unit tests (wired into test_ubuilder)
```

### 4.3 Public API sketch

```c
// config.h
typedef struct ub_config_file_t ub_config_file_t;

// Load and validate. Returns UB_SUCCESS even if file is absent (out=NULL).
// Returns error only on parse/validate failure or explicit --config path missing.
ub_result_t ub_config_load(const char* explicit_path,
                           const char* project_dir_hint,
                           ub_config_file_t** out);

// Apply config values to `cfg`, but only where `cfg` fields are still defaults
// (i.e. were not set by the CLI). CLI-set fields are preserved.
ub_result_t ub_config_apply_to(const ub_config_file_t* file, ub_config_t* cfg);

void ub_config_free(ub_config_file_t* file);
```

### 4.4 main.c integration

```
parse_arguments(argc, argv, &cli_cfg);        // existing
ub_config_load(cli_cfg.config_path,
               cli_cfg.project_dir,
               &cfg_file);                    // new
ub_config_apply_to(cfg_file, &cli_cfg);       // new — fills in unset fields
validate(&cli_cfg);                           // existing path
```

The "was this CLI flag set" tracking is a small change to `parse_arguments`: each setter also writes to a `cli_cfg.set_flags` bitfield, which `ub_config_apply_to` reads.

### 4.5 Tests

- `tests/test_config.c` (unit):
  - Round-trip every key.
  - Reject unknown top-level key only with a warning, not a failure.
  - Reject type mismatch with an error.
  - Verify CLI > config precedence: pass `--runtime=php` with a `"runtime": "python"` config, expect PHP wins.
  - Verify `--config` to a missing file is an error; bare CWD with no config is fine.
- `tests/bundle/fixtures/*/ubuilder.json` already exists — once the parser lands, the harness can be updated to drop redundant `--runtime` / `--entry-point` flags as a smoke test of the discovery path.

### 4.6 Build system

`src/CMakeLists.txt`: add `core/config.c` to `ubuilder_core` sources; add `vendor/jsmn` to includes.

No new external CMake `find_package` calls. No new compile flags.

## 5. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Config drift — users edit `ubuilder.json` but CI runs with `--project-dir` and ignores it | Document the precedence prominently in `--help`; emit one-line "using config from <path>" to stderr in non-quiet mode. |
| Glob portability | Vendor the matcher (~80 LOC) rather than relying on `fnmatch` (POSIX only) or `PathMatchSpecW` (Windows only). |
| Bad error messages | jsmn gives offsets; convert to line:col in a single 20-line helper. Sample: `ubuilder.json:5:14: expected string, got number for "entry_point"`. |
| Schema bloat over time | The `schema_version` reservation lets us require a version bump for breaking changes. Unknown keys warn rather than fail so additive evolution is painless. |
| Two paths to set the same value invite confusion | Mitigate by always echoing the *resolved* config (post-merge) when `--verbose` is set. |

## 6. Future extensions (out of scope for v1)

- **`ubuilder.toml` alternative**: TOML is friendlier for human-edited config. If/when demand appears, support both — prefer `.toml` over `.json` if both exist. Don't pre-vendor a TOML parser; defer until needed.
- **`extends`**: `{"extends": "../base.ubuilder.json"}` for monorepos. Deferred — easy to add, no demand yet.
- **Env interpolation**: `"output": "dist/myapp-${VERSION}"`. Deferred; users can wrap with a shell script today.
- **Per-target overrides**: `targets.linux.x86_64`, `targets.windows.x86_64`. Deferred until audit M1 (hermetic interpreters per platform/arch) lands.
- ~~**`--init` subcommand**: `ubuilder --init python` writes a starter `ubuilder.json`.~~ Superseded: a successful build with no `ubuilder.json` auto-writes one (`ub_config_write_if_missing` in `src/core/config.c`). A future explicit `--init` could still be useful for projects that want to seed `ubuilder.json` without running a build.
- **Lockfile**: `ubuilder.lock` capturing interpreter SHA-256 for reproducibility. Deferred until audit M3/M5 (versioned container + content-addressed cache).

## 7. Acceptance criteria for v1

- `cd tests/bundle/fixtures/python && ubuilder` (no flags) produces a working bundle when run from a checkout with the parser landed.
- `tests/bundle/run-bundle-tests.sh` can have its per-runtime `--runtime` flag removed (relying on config) and still pass.
- `tests/test_config` passes the unit cases above.
- `--help` documents `--config` and the discovery order.
- Audit §3 G7 (broken argv quoting) is *not* a blocker — config-file work is parallel; both should land before users routinely build large projects.
