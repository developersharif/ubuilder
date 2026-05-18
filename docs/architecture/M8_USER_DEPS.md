# M8 — User Dependency Installation

**Status:** ✅ Python end-to-end (M8-A). Node `package.json` (M8-B) and PHP `composer.json` (M8-C) deferred.
**Companion to:** `M1_HERMETIC_INTERPRETERS.md` (M8 builds on M1's vendored runtimes).
**Goal:** real-world apps with `numpy` / `requests` / `attrs` etc. produce a hermetic bundle that imports those packages — without polluting the shared runtime cache.

## The problem M8 solves

After M1, `ubuilder` produces hermetic Python bundles using a vendored interpreter from `python-build-standalone`. But that interpreter has only the Python stdlib — no `pip install`'d packages from the user's project. Real apps need their deps.

The naive fix (pip-install into the cache) breaks under the Apple-sandbox principle: the next bundle for a different project would silently pick up the previous project's deps. The shared cache must stay clean.

## How M8 works

When the build runs and the project has a dependency manifest (today: `requirements.txt`), UBuilder:

1. **Stages the runtime.** Hardlinks (or copies, on cross-device fallback) the entire vendored tree from `~/.cache/ubuilder/runtimes/python/<version>/` into `~/.cache/ubuilder/stage/build-<pid>/`. Hardlinking is almost-free; pip's `--target=` writes only new files so existing inodes are never mutated. The shared cache is read-only from this point on.

2. **Runs the staged interpreter to install deps:**
   ```
   <stage>/bin/python3 -m pip install \
       --disable-pip-version-check \
       --no-warn-script-location \
       --target <stage>/lib/python3.X/site-packages \
       -r <project>/requirements.txt
   ```
   Spawned via `pc_spawn_and_wait` — no shell, structured argv, deterministic env. The staged Python (not the host's) does the install, so packages are installed for the right interpreter version.

3. **Embeds the staged tree** via `ub_embed_runtime_tree`. The bundle now contains the interpreter *and* the user's deps.

4. **Cleans up the stage** via `pc_remove_tree`. The cache is exactly as it started.

The whole flow is automatic — no flags needed. Opt out with `--no-install-deps` (skip even if `requirements.txt` exists).

## Verified end-to-end

Fixture: `tests/bundle/fixtures/python-with-deps/`
- `main.py`: `import attrs; print(attrs.__version__)`
- `requirements.txt`: `attrs==23.2.0`

Bundle harness (`tests/bundle/run-bundle-tests.sh`) asserts:
1. Bundle builds (≈193 MB, +8 MB over the no-deps hermetic bundle).
2. Bundle runs locally and prints `attrs:23.2.0`.
3. Bundle runs with `PATH=/nonexistent` — full hermeticity preserved.
4. **Vendored cache stays clean** — `find <cache>/lib -name attrs -type d` returns nothing after the build.

If staging ever silently writes to the shared cache, assertion #4 fails loudly.

## Honors the Apple-sandbox principle

- **Clause 1** (no `system()`/`popen()`): pip is spawned via `pc_spawn_and_wait` with an explicit argv array. No shell involved.
- **Clause 4** (hermetic builds): bundle output is a function of declared inputs (the vendored runtime hash + the project's `requirements.txt`). The user's local pip cache and host site-packages are not consulted.
- **Clause 6** (isolation): staging never mutates the shared cache. Future M8 versions can build a content-addressed staging cache keyed by `sha256(requirements.txt)` for fast rebuilds — the principle stays the same.

## Limitations & deferred work

| Item | Status | Notes |
|---|---|---|
| Pure-Python wheels (`attrs`, `requests`, ...) | ✅ works | Tested with `attrs==23.2.0`. |
| Compiled wheels (`numpy`, `pillow`, ...) | ✅ should work | pip downloads `manylinux2014` wheels by default; staging interpreter matches Python 3.12 ABI. Not yet tested in CI. |
| Node `package.json` (M8-B) | deferred | Same shape: stage the Node tree, run `npm ci --omit=dev --prefix <stage>` ... no, npm installs to `node_modules` relative to cwd. Easiest path: stage, then `cd <stage>` and `npm ci`. |
| PHP `composer.json` (M8-C) | deferred | Waits for upstream hermetic PHP (M1-D blocker). |
| Lock file (`requirements.lock`) | deferred | Today's bundle reproducibility = `requirements.txt` content + the vendored interpreter hash. A real lockfile would freeze transitive deps too. |
| Content-addressed install cache | deferred | First M8 build is slow (pip download). Future: cache `<dep-set-hash> → installed site-packages` so repeat builds skip pip. |
| Build-time network gate | deferred | pip needs PyPI access at build time. Document this; add an `--offline-pip-cache=<dir>` flag later for air-gapped builds. |

## Configuration

| Surface | Effect |
|---|---|
| `--no-install-deps` CLI flag | Skip the staging+install step even if `requirements.txt` is present. |
| `runtime_options.python.no_install_deps: true` in `ubuilder.json` | Same, but checked in with the project. |
| `requirements.txt` absent | Stage step is skipped automatically (no overhead). |

CLI flag takes precedence over the config file (same model as every other M1/M8 field).
