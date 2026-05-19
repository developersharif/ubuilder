# Roadmap — Best-DX Items For Next Sessions

**Purpose:** capture the remaining best-DX items with enough concrete context that a future session can pick them up cold without re-deriving the design.
**Order:** roughly by user-visible impact.

## Status snapshot

- ✅ #1 README rewrite — landed in commit `34e859d`
- ✅ #2 Content-addressed install cache — landed in commit `588f19d`
- ✅ #3 Lockfile reproducibility — landed in commit `588f19d` (bundled with #2)
- ⏳ #4 M1-D PHP hermetic — open (needs CI workflow building static-php-cli)
- ⏳ #5 arm64 + macOS vendor entries — open (untestable on Linux x86_64 dev host)
- ✅ #6 Auto-vendor download UX polish — landed in commit `753a836`
- ⏳ #7 `ubuilder init` subcommand — open (lowest priority)

Remaining items in this doc are kept for future-session scope.

---

## 1. README + onboarding rewrite

**Why now:** the user story has shifted from "build with these N flags" to literally `ubuilder` with zero flags. The current root `README.md` still describes the pre-M1 / pre-S10 world (CMake build, then `--project-dir --runtime --output`). Anyone landing on the repo gets the wrong mental model.

**Concrete scope:**
- Top-of-fold "what is this" sentence rewritten around: "drop a Python / Node app, run `ubuilder`, get a single hermetic executable that runs anywhere."
- "Quick Start" section becomes a single command (`ubuilder`).
- "Build UBuilder" section moved below the user-story content (most users will install a release, not build from source).
- New "How it works" section linking out to the audit + M1 + M8 docs.
- Drop the misleading "Currently in Phase 1" line — we're well past that.
- Bundle harness output snippet to show what success looks like (Python attrs + Node picocolors).

**Files:** `README.md` (root). Possibly `docs/README.md` (already mostly correct).
**Verify:** read it fresh; can a stranger figure out what UBuilder does and how to use it in < 30 seconds?

---

## 2. Content-addressed install cache (M8 fast-path)

**Why:** Today, every M8 build re-runs `pip install` / `npm install` even if `requirements.txt` / `package.json` is unchanged. For an app with many deps this is the slow step. The first build downloads them, the second build downloads them *again*.

**Concrete design:**
- Compute `sha256(requirements.txt content || vendored Python tree hash)` → cache key.
- Cache layout: `$XDG_CACHE_HOME/ubuilder/install-cache/python/<sha>/site-packages/...`.
- Before staging, check the cache. If hit, hardlink the cached site-packages contents into the stage instead of running pip.
- After a successful install (cache miss), copy the resulting site-packages into the cache.
- Same shape for Node (`sha256(package.json + package-lock.json)` → cached `node_modules/`).

**Touch points:**
- `python_maybe_stage_with_deps` in `src/runtimes/python_builder.c`
- `nodejs_maybe_stage_project_with_deps` in `src/runtimes/nodejs_builder.c`
- New helper `ub_install_cache_lookup(key, dest)` / `ub_install_cache_store(key, src)` in runtime_embedder
- Reuse `pc_copy_or_link_tree` for the hardlink-copy

**Verify:** time `ubuilder` twice on a `requirements.txt` with 10 deps. Second build should skip pip entirely (look for absence of "Downloading attrs..." in stderr).

**Risk:** cache invalidation on interpreter version change. Including the runtime-tree SHA in the key avoids stale-cache bugs when the user `vendor-runtimes.sh` updates Python.

---

## 3. Lockfile reproducibility

**Why:** `attrs==23.2.0` pins direct deps but not transitive ones. Two builds a month apart can pull different transitive dep versions, breaking reproducibility. Real lockfiles (`requirements.lock`, `package-lock.json`) freeze the whole resolution graph.

**Concrete scope:**
- **Python:** if `<project>/requirements.lock` exists, prefer it over `requirements.txt`. Use `pip install --no-deps -r requirements.lock` (lockfile already has resolved transitive deps).
- **Node:** already partly works — npm picks up `package-lock.json` automatically. Switch our spawn from `npm install --omit=dev` to `npm ci --omit=dev` (which *requires* a lockfile and is faster). Document that users should check in `package-lock.json`.

**Touch points:** `python_pip_install` argv array; nodejs npm argv array; spec doc update.
**Verify:** create a fixture with both `requirements.txt` and `requirements.lock`. Build twice on different days (mock by tweaking system clock?). Both should produce byte-identical bundles modulo the SHA-256 trailer.

---

## 4. M1-D — PHP hermetic

**The blocker:** no upstream pre-built static PHP exists. `crazywhalecc/static-php-cli` produces one via a self-build (~10 min). Two paths to ship:

**Option A (preferred, faster shipping):** build static PHP ourselves in a GitHub Actions workflow, attach the resulting binary to a UBuilder release, vendor from there.
- Add `.github/workflows/build-static-php.yml` that runs `static-php-cli`, uploads `php-<ver>-linux-x86_64.tar.gz` as a release asset.
- Add the vendor entry in `scripts/vendor-runtimes.sh`.
- `php_builder.c` already has the M1 hermetic plumbing — just needs the cache entry.

**Option B (slower):** invoke `static-php-cli` inside `vendor-runtimes.sh` if the user explicitly asks (`vendor-runtimes.sh php --build`). Documented escape hatch; doesn't help the auto-vendor default.

**Verify:** `ubuilder` on a PHP fixture → hermetic bundle → runs in `debian:12-slim` Tier-3.

---

## 5. arm64 + macOS vendor entries

**Why:** Linux x86_64 is the only platform-arch with vendored entries today. The vendoring upstream sources all publish multiple targets:
- python-build-standalone: `aarch64-unknown-linux-gnu`, `aarch64-apple-darwin`, `x86_64-apple-darwin`, `x86_64-pc-windows-msvc`.
- nodejs.org: `linux-arm64`, `darwin-x64`, `darwin-arm64`, `win-x64`.

**Concrete scope:**
- Extend `scripts/vendor-runtimes.sh` manifest with per-platform/arch URLs + SHAs (manifest is already gated by `$PLATFORM`/`$ARCH`).
- Add macOS-specific code paths where needed in `python_builder.c` / `nodejs_builder.c` (mostly the embed flow is platform-agnostic, but path separators and exec bits might bite).
- Tier-3 CI: add a macOS runner that runs the bundle in a Docker container (Docker on macOS uses a Linux VM, so debian:12-slim still works) OR a separate macOS-native sandbox test.

**Risk:** none of this is testable on this dev machine. Plan to verify in CI first, then claim it locally.

---

## 6. Auto-vendor: download progress UX polish

**Why:** Today the script's curl output is verbose and noisy when ubuilder spawns it via auto-vendor. The header "Auto-vendoring python (one-time setup) ..." appears but then 30 seconds of curl progress chatter floods the screen.

**Concrete scope:**
- Pass `--silent --show-error --no-progress-meter` to curl in `vendor-runtimes.sh` when stdout is not a TTY (or always, with a `UBUILDER_VENDOR_VERBOSE` env to re-enable).
- Print a single line from ubuilder: `Downloading python 3.12.13 (~32 MB)... done in 16.2s`.
- Buffer the script's stderr so we can show it only on failure.

**Touch points:** `scripts/vendor-runtimes.sh`, `ub_auto_vendor` in `runtime_embedder.c`.
**Verify:** wipe cache, run `ubuilder`; stdout should be 3-5 lines of progress, not 30.

---

## 7. `ubuilder init` subcommand (later)

**Why:** users who don't have an `ubuilder.json` would benefit from `ubuilder init python` (or `node`) that drops a starter `ubuilder.json` + a minimal `main.py` / `main.js`.

**Out of scope for now** — useful but not blocking. File as a follow-up.

---

## Pre-flight checklist for tomorrow

When picking up:

1. `cd "/home/sharif/Documents/Codes/c lang/ubuilder"`
2. `git status` — confirm last commit is the auto-vendor work (`f03e830` or descendant).
3. `./build/tests/test_ubuilder` — must pass 101/101.
4. `tests/bundle/run-bundle-tests.sh` — must pass 7/7.
5. `tests/bundle/run-tier3.sh` — must pass 2/2 (needs Docker daemon).
6. Read `docs/architecture/ARCHITECTURE_AUDIT.md` top section (the Apple-sandbox-rule binding principle).
7. Pick one item from above. Item #1 (README rewrite) is the cheapest user-facing win.

## Memory hooks already in place

`~/.claude/projects/-home-sharif-Documents-Codes-c-lang-ubuilder/memory/` has:
- `feedback_apple_sandbox_rule.md` — the binding hermeticity discipline.
- `project_docker_desktop.md` — Docker Desktop only shares HOME by default; bind-mount sources can't live in `/tmp`.

If a new constraint comes up tomorrow that future sessions should know, save it as a new memory file and link from `MEMORY.md`.
