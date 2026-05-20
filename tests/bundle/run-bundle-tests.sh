#!/usr/bin/env bash
# tests/bundle/run-bundle-tests.sh
# End-to-end bundle harness: build ubuilder -> bundle each fixture -> run the
# bundle in a clean directory -> assert exit code and stdout.
#
# Usage:
#   tests/bundle/run-bundle-tests.sh                  # run all runtimes
#   tests/bundle/run-bundle-tests.sh python php       # filter
#   UBUILDER_KEEP_ARTIFACTS=1 ... run-bundle-tests.sh # don't delete output on success
#   UBUILDER_VERBOSE=1 ... run-bundle-tests.sh        # echo every command
#
# Exit code is the number of failed cases (0 on full success, capped at 125).

set -u
[[ "${UBUILDER_VERBOSE:-0}" == "1" ]] && set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${UBUILDER_BUILD_DIR:-$REPO_ROOT/build}"
# Allow overriding the launcher under test — useful for the S8 static build,
# CI artifacts, or any pre-built ubuilder you want to exercise.
UBUILDER_BIN="${UBUILDER_BIN:-$BUILD_DIR/src/ubuilder}"
FIXTURE_DIR="$SCRIPT_DIR/fixtures"
WORK_DIR="$(mktemp -d -t ubuilder-bundle-tests.XXXXXX)"
RUNTIMES_CACHE="${UBUILDER_RUNTIMES_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/runtimes}"

# ---------- output helpers ----------
if [[ -t 1 ]]; then
    C_RED='\033[0;31m'; C_GRN='\033[0;32m'; C_YEL='\033[1;33m'; C_DIM='\033[2m'; C_RST='\033[0m'
else
    C_RED=''; C_GRN=''; C_YEL=''; C_DIM=''; C_RST=''
fi
log()  { printf '%b\n' "$*"; }
info() { log "${C_DIM}[harness]${C_RST} $*"; }
ok()   { log "${C_GRN}  ✓${C_RST} $*"; }
fail() { log "${C_RED}  ✗${C_RST} $*"; }
warn() { log "${C_YEL}  !${C_RST} $*"; }

# ---------- argument filter ----------
ALL_RUNTIMES=(python php nodejs)
if (($# > 0)); then
    REQUESTED=("$@")
else
    REQUESTED=("${ALL_RUNTIMES[@]}")
fi

# ---------- preflight: build ubuilder if missing ----------
ensure_ubuilder() {
    if [[ -x "$UBUILDER_BIN" ]]; then
        info "ubuilder present at $UBUILDER_BIN"
        return 0
    fi
    info "ubuilder not built; configuring + building"
    mkdir -p "$BUILD_DIR"
    ( cd "$BUILD_DIR" && cmake -DCMAKE_BUILD_TYPE=Release "$REPO_ROOT" >/dev/null )
    cmake --build "$BUILD_DIR" -j --target ubuilder >/dev/null
    if [[ ! -x "$UBUILDER_BIN" ]]; then
        fail "ubuilder still missing after build"
        return 1
    fi
}

# ---------- preflight: host runtime present (today's bundles still need it) ----------
host_runtime_for() {
    case "$1" in
        python) command -v python3 || command -v python ;;
        php)    command -v php ;;
        nodejs) command -v node ;;
        *)      return 1 ;;
    esac
}

# ---------- per-runtime fixture metadata ----------
# Each fixture directory holds main.{py,php,js} plus expected.txt.
fixture_dir_for() {
    case "$1" in
        python) printf '%s/python\n' "$FIXTURE_DIR" ;;
        php)    printf '%s/php\n' "$FIXTURE_DIR" ;;
        nodejs) printf '%s/nodejs\n' "$FIXTURE_DIR" ;;
    esac
}

runtime_flag_for() {
    # ubuilder accepts: python, php, node
    case "$1" in
        python) printf 'python\n' ;;
        php)    printf 'php\n' ;;
        nodejs) printf 'node\n' ;;
    esac
}

# ---------- the single test routine ----------
# Args: <runtime-key>
# Stages:
#   1. Bundle the fixture
#   2. Move the bundle into an empty run directory (clean CWD)
#   3. Run with deterministic args; capture stdout + exit code
#   4. Compare stdout to expected.txt, exit code to 0
run_case() {
    local rt="$1"
    local fdir; fdir="$(fixture_dir_for "$rt")"
    local rflag; rflag="$(runtime_flag_for "$rt")"
    local case_work="$WORK_DIR/$rt"
    local bundle_out="$case_work/build/app"
    local run_dir="$case_work/run"

    log ""
    log "── $rt ──"

    if [[ ! -d "$fdir" ]]; then
        fail "fixture missing: $fdir"
        return 1
    fi
    if [[ ! -f "$fdir/expected.txt" ]]; then
        fail "fixture missing expected.txt: $fdir/expected.txt"
        return 1
    fi
    if ! host_runtime_for "$rt" >/dev/null; then
        warn "host $rt runtime missing — current builders need it on the build host; skipping"
        return 0
    fi

    # PHP M1-D synthesizes a runtime from the host PHP + extension_dir
    # layout. On Linux that means Debian/RHEL/Arch-style enumeration; on
    # macOS the builder auto-detects statically linked host PHP (Laravel
    # Herd, static-php-cli) and ships the binary as-is. Dynamic Homebrew
    # PHP on macOS is still unsupported — its Cellar layout + Mach-O dyld
    # rewiring of bundled dylibs are future work. If you're on macOS with
    # dynamic Homebrew PHP, the bundle step will fail at extension_dir
    # enumeration; that's expected until M1-D-macos-dynamic lands.

    mkdir -p "$case_work/build" "$run_dir"

    # 1. bundle — relies on ubuilder.json discovery for runtime + entry_point.
    #    Only --project-dir and --output come from the CLI, exercising the
    #    config-file path that the spec promises.
    info "bundling $fdir -> $bundle_out (via ubuilder.json discovery)"
    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --output="$bundle_out" \
            >"$case_work/build.log" 2>&1; then
        fail "ubuilder build failed (see $case_work/build.log)"
        sed 's/^/    /' "$case_work/build.log" | tail -n 20
        return 1
    fi
    if [[ ! -x "$bundle_out" ]]; then
        fail "bundle missing or not executable: $bundle_out"
        return 1
    fi
    ok "bundle built ($(du -h "$bundle_out" | cut -f1))"

    # 2. move into empty run directory — CWD must not contain the fixture
    cp "$bundle_out" "$run_dir/app"
    chmod +x "$run_dir/app"

    # 3. run — pass tricky argv values to verify pass-through is correct.
    #    "hello world" (embedded space) and "it's" (single quote) would both
    #    break the pre-S1 system()/strncat path. The post-S1 posix_spawn
    #    path delivers them verbatim.
    info "running bundle from $run_dir"
    local stdout_path="$case_work/stdout.txt"
    local stderr_path="$case_work/stderr.txt"
    local exit_code=0
    ( cd "$run_dir" && ./app "hello world" "it's" "ok" ) \
        >"$stdout_path" 2>"$stderr_path" || exit_code=$?

    # 4. assert
    local rc=0
    if (( exit_code != 0 )); then
        fail "exit code $exit_code (expected 0)"
        sed 's/^/    [stderr] /' "$stderr_path" | tail -n 20
        rc=1
    fi

    if ! diff -u "$fdir/expected.txt" "$stdout_path" >"$case_work/stdout.diff" 2>&1; then
        fail "stdout differs from expected"
        sed 's/^/    /' "$case_work/stdout.diff" | head -n 40
        rc=1
    fi

    if (( rc == 0 )); then
        ok "exit code 0 and stdout matches expected.txt"
    fi

    # macOS portability assertion: walk every Mach-O in the extracted
    # bundle tree and confirm every LC_LOAD_DYLIB reference is either
    # system (/usr/lib, /System) or @executable_path-relative. Catches
    # /opt/homebrew/... or /usr/local/... leakage that would break the
    # bundle on a Mac without that path. Currently scoped to PHP because
    # that's the runtime whose builder bundles host dylibs; Python/Node
    # use vendored hermetic trees with @executable_path refs from upstream.
    if (( rc == 0 )) && [[ "$rt" == "php" && "$(uname)" == "Darwin" ]]; then
        local portcheck="$REPO_ROOT/tests/bundle/assert-macos-portable.sh"
        if [[ -x "$portcheck" ]]; then
            # PORTCHECK_VERBOSE=1 ensures the scan emits a [scan] line per
            # Mach-O so when CI fails we can see how far the script got.
            if ! PORTCHECK_VERBOSE=1 "$portcheck" "$run_dir/app" >"$case_work/portcheck.log" 2>&1; then
                fail "macOS portability check failed (see $case_work/portcheck.log)"
                echo "    --- portcheck.log size $(wc -c < "$case_work/portcheck.log" 2>/dev/null || echo '?') bytes ---"
                # cat (not tail) — these CI failures keep showing empty
                # tails. Show the whole thing so we stop guessing.
                sed 's/^/    /' "$case_work/portcheck.log"
                echo "    --- end portcheck.log ---"
                rc=1
            else
                ok "all Mach-O refs are @executable_path-relative or system"
            fi
        fi
    fi

    # 5. Tamper test (S3 — Apple-sandbox integrity rule).
    #    Flip one byte in the middle of the file, which for our multi-MB
    #    bundles is always inside the payload region (the launcher binary
    #    is < 1 MB; the trailer is < 100 bytes at the tail). Post-S3
    #    bundles MUST refuse to run with non-zero exit and a clear
    #    "integrity check FAILED" diagnostic.
    if (( rc == 0 )); then
        local tamper_bin="$run_dir/app.tampered"
        cp "$run_dir/app" "$tamper_bin"
        # `stat -c %s` is GNU; BSD/macOS uses `stat -f %z`. wc works on both.
        local file_size; file_size="$(wc -c < "$tamper_bin" | tr -d ' ')"
        local mid=$(( file_size / 2 ))
        printf '\xAA' | dd of="$tamper_bin" bs=1 seek="$mid" count=1 conv=notrunc \
            >/dev/null 2>&1
        chmod +x "$tamper_bin"
        local tamper_exit=0
        ( "$tamper_bin" >/dev/null 2>"$case_work/tamper.stderr" ) || tamper_exit=$?
        # On macOS the kernel/Gatekeeper rejects executables whose
        # ad-hoc signature no longer matches the contents — typical exit
        # is 126 (cannot execute), 137 (SIGKILL via taskgated), or
        # variants. The tamper is still "caught"; our own integrity check
        # just doesn't get a chance to print because the binary never
        # starts. Accept any non-zero exit as success on Darwin; require
        # the explicit 'integrity check FAILED' marker only elsewhere.
        local on_macos=0
        [[ "$(uname)" == "Darwin" ]] && on_macos=1
        if (( tamper_exit == 0 )); then
            fail "tampered bundle ran successfully — integrity check did not fire"
            rc=1
        elif (( on_macos )); then
            ok "tampered bundle correctly refused to run (exit $tamper_exit; OS-level signature check on macOS)"
        elif ! grep -q 'integrity check FAILED' "$case_work/tamper.stderr"; then
            fail "tampered bundle exited $tamper_exit but stderr lacks 'integrity check FAILED'"
            sed 's/^/    [stderr] /' "$case_work/tamper.stderr" | tail -n 10
            rc=1
        else
            ok "tampered bundle correctly refused to run (exit $tamper_exit, integrity error)"
        fi
    fi
    return $rc
}

# ---------- main ----------
cleanup() {
    if [[ "${UBUILDER_KEEP_ARTIFACTS:-0}" == "1" ]]; then
        info "keeping artifacts under $WORK_DIR"
    else
        rm -rf "$WORK_DIR"
    fi
}
trap cleanup EXIT

info "repo:    $REPO_ROOT"
info "build:   $BUILD_DIR"
info "work:    $WORK_DIR"
info "running: ${REQUESTED[*]}"

ensure_ubuilder || exit 2

failures=0
for rt in "${REQUESTED[@]}"; do
    case "$rt" in
        python|php|nodejs) ;;
        *) warn "unknown runtime '$rt' — skipping"; continue ;;
    esac
    run_case "$rt" || ((failures++))
done

# M1-B/E (hermetic runtime). The headline test of the entire M1 program:
# build a bundle using a vendored interpreter, then run it with PATH
# stripped to prove the bundle does NOT need the host runtime. Parametric
# so Python and Node share the same harness logic.
#   $1 = runtime key (python / nodejs)
#   $2 = cache subdir under $RUNTIMES_CACHE (python / node)
#   $3 = relative exe inside the tree (bin/python3 / bin/node)
run_hermetic_case() {
    local rt="$1" cache_subdir="$2" rel_exe="$3"
    local fdir="$FIXTURE_DIR/$rt"
    local hermetic_dir
    hermetic_dir="$(find "$RUNTIMES_CACHE/$cache_subdir" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)"

    log ""
    log "── $rt (hermetic via --runtime-source) ──"
    if [[ -z "$hermetic_dir" || ! -x "$hermetic_dir/$rel_exe" ]]; then
        warn "vendored $rt not found at $RUNTIMES_CACHE/$cache_subdir/*"
        warn "skipping; run scripts/vendor-runtimes.sh $cache_subdir first"
        return 0
    fi

    local cw="$WORK_DIR/$rt-hermetic"
    mkdir -p "$cw/build" "$cw/run"
    info "vendored runtime: $hermetic_dir"

    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --runtime-source="$hermetic_dir" \
            --output="$cw/build/app" \
            >"$cw/build.log" 2>&1; then
        fail "ubuilder build failed (see $cw/build.log)"
        sed 's/^/    /' "$cw/build.log" | tail -n 20
        return 1
    fi
    ok "bundle built ($(du -h "$cw/build/app" | cut -f1))"

    cp "$cw/build/app" "$cw/run/app"
    chmod +x "$cw/run/app"

    # The actual hermeticity proof: PATH points at an empty dir, so the
    # only interpreter available is the one inside the bundle.
    local fake_path
    fake_path="$(mktemp -d)"
    local exit_code=0
    ( cd "$cw/run" && PATH="$fake_path" ./app "hello world" "it's" "ok" ) \
        >"$cw/stdout.txt" 2>"$cw/stderr.txt" || exit_code=$?
    rmdir "$fake_path"

    local rc=0
    if (( exit_code != 0 )); then
        fail "hermetic bundle exited $exit_code (expected 0)"
        sed 's/^/    [stderr] /' "$cw/stderr.txt" | tail -n 10
        rc=1
    fi
    if ! diff -u "$fdir/expected.txt" "$cw/stdout.txt" >"$cw/stdout.diff" 2>&1; then
        fail "hermetic bundle stdout differs from expected"
        sed 's/^/    /' "$cw/stdout.diff" | head -n 30
        rc=1
    fi
    if (( rc == 0 )); then
        ok "hermetic bundle runs with PATH stripped — true zero-host-dep"
    fi
    return $rc
}

# Run a hermetic case per requested runtime that has a vendor entry today.
# PHP intentionally skipped — no upstream pre-built static PHP yet (see
# docs/architecture/M1_HERMETIC_INTERPRETERS.md).
for rt in "${REQUESTED[@]}"; do
    case "$rt" in
        python) run_hermetic_case python python "bin/python3" || ((failures++)) ;;
        nodejs) run_hermetic_case nodejs node   "bin/node"    || ((failures++)) ;;
    esac
done

# M8 (dependency install). The dep-bundling proof: a fixture that imports
# `attrs` (not in the stdlib) is pip-installed into the staged hermetic
# runtime, then runs with PATH stripped. Network required at build time.
run_m8_deps_case() {
    local hermetic_dir
    hermetic_dir="$(find "$RUNTIMES_CACHE/python" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)"
    log ""
    log "── python-with-deps (M8: pip install into hermetic tree) ──"
    if [[ -z "$hermetic_dir" || ! -x "$hermetic_dir/bin/python3" ]]; then
        warn "vendored Python missing; run scripts/vendor-runtimes.sh python first"
        return 0
    fi

    local fdir="$FIXTURE_DIR/python-with-deps"
    local cw="$WORK_DIR/python-with-deps"
    mkdir -p "$cw/build" "$cw/run"
    info "fixture: $fdir (requirements.txt → attrs==23.2.0)"

    # M8-fast cache hygiene: wipe the install cache before the first build
    # so we can prove cache miss → store, then cache hit on the second build.
    local ic_root="${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/install-cache/python"
    rm -rf "$ic_root"

    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --output="$cw/build/app" \
            >"$cw/build.log" 2>&1; then
        fail "ubuilder build failed (see $cw/build.log)"
        sed 's/^/    /' "$cw/build.log" | tail -n 20
        return 1
    fi
    ok "bundle built ($(du -h "$cw/build/app" | cut -f1))"
    if ! grep -q "Install cache stored (python/" "$cw/build.log"; then
        fail "build #1 did not store an install-cache entry (see $cw/build.log)"
        return 1
    fi
    ok "build #1 stored install-cache entry"

    # Build #2: same fixture, expect cache hit and no pip install.
    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --output="$cw/build/app2" \
            >"$cw/build2.log" 2>&1; then
        fail "ubuilder build #2 failed (see $cw/build2.log)"
        sed 's/^/    /' "$cw/build2.log" | tail -n 20
        return 1
    fi
    if ! grep -q "Install cache hit (python/" "$cw/build2.log"; then
        fail "build #2 missed the install cache (see $cw/build2.log)"
        return 1
    fi
    if grep -qE "^Installing Python dependencies" "$cw/build2.log"; then
        fail "build #2 ran pip install despite cache hit"
        return 1
    fi
    ok "build #2 hit install cache (no pip install run)"

    cp "$cw/build/app" "$cw/run/app"
    chmod +x "$cw/run/app"

    local fake_path; fake_path="$(mktemp -d)"
    local exit_code=0
    ( cd "$cw/run" && PATH="$fake_path" ./app "hello world" "it's" "ok" ) \
        >"$cw/stdout.txt" 2>"$cw/stderr.txt" || exit_code=$?
    rmdir "$fake_path"

    local rc=0
    if (( exit_code != 0 )); then
        fail "exit $exit_code (expected 0)"
        sed 's/^/    [stderr] /' "$cw/stderr.txt" | tail -n 10
        rc=1
    fi
    if ! diff -u "$fdir/expected.txt" "$cw/stdout.txt" >"$cw/stdout.diff" 2>&1; then
        fail "stdout differs"
        sed 's/^/    /' "$cw/stdout.diff" | head -n 30
        rc=1
    fi

    # Assert the cache stayed clean — `attrs` must NOT have been installed
    # into the shared vendored tree. M8's staging promise is broken if it has.
    if [[ -d "$hermetic_dir/lib" ]]; then
        if find "$hermetic_dir/lib" -name 'attrs' -type d 2>/dev/null | grep -q .; then
            fail "vendored cache was polluted with 'attrs' — staging did not isolate"
            rc=1
        fi
    fi

    if (( rc == 0 )); then
        ok "bundle imports attrs (installed at build), runs with PATH stripped, cache untouched"
    fi
    return $rc
}

# M8 only meaningful when python is in the requested list (or default-all).
for rt in "${REQUESTED[@]}"; do
    if [[ "$rt" == "python" ]]; then
        run_m8_deps_case || ((failures++))
        break
    fi
done

# Python --exclude on a wheel: requirements.txt with two deps, build with
# --exclude=six → filter strips the six line before pip runs, bundle runs
# with attrs but fails to import six.
run_python_exclude_case() {
    local hermetic_dir
    hermetic_dir="$(find "$RUNTIMES_CACHE/python" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)"
    log ""
    log "── python-with-exclude (--exclude=<wheel> filters requirements.txt) ──"
    if [[ -z "$hermetic_dir" || ! -x "$hermetic_dir/bin/python3" ]]; then
        warn "vendored Python missing; skipping"
        return 0
    fi

    local fdir="$FIXTURE_DIR/python-with-exclude"
    local cw="$WORK_DIR/python-with-exclude"
    mkdir -p "$cw/build" "$cw/run"
    info "fixture: $fdir (requirements: attrs+six; --exclude=six)"

    rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/install-cache/python"

    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --output="$cw/build/app" \
            --exclude=six \
            >"$cw/build.log" 2>&1; then
        fail "build failed"
        sed 's/^/    /' "$cw/build.log" | tail -n 20
        return 1
    fi
    if ! grep -q "Filtered 1 line(s) from requirements via --exclude" "$cw/build.log"; then
        fail "build log missing 'Filtered 1 line(s)' marker"
        sed 's/^/    /' "$cw/build.log" | tail -n 20
        return 1
    fi
    if ! grep -q "six (excluded by config; not installed)" "$cw/build.log"; then
        fail "build log missing per-package excluded notice"
        return 1
    fi
    ok "build #1 filtered six from requirements before pip ran"

    cp "$cw/build/app" "$cw/run/app"; chmod +x "$cw/run/app"
    local fake_path; fake_path="$(mktemp -d)"
    local exit_code=0
    ( cd "$cw/run" && PATH="$fake_path" ./app "hello world" "it's" "ok" ) \
        >"$cw/stdout.txt" 2>"$cw/stderr.txt" || exit_code=$?
    rmdir "$fake_path"

    local rc=0
    if (( exit_code != 0 )); then
        fail "exit $exit_code (expected 0)"
        sed 's/^/    [stderr] /' "$cw/stderr.txt" | tail -n 10
        rc=1
    fi
    if ! diff -u "$fdir/expected.txt" "$cw/stdout.txt" >"$cw/stdout.diff" 2>&1; then
        fail "stdout differs"
        sed 's/^/    /' "$cw/stdout.diff" | head -n 20
        rc=1
    fi
    if (( rc == 0 )); then
        ok "bundle imports attrs and proves six is absent at runtime"
    fi
    return $rc
}

for rt in "${REQUESTED[@]}"; do
    if [[ "$rt" == "python" ]]; then
        run_python_exclude_case || ((failures++))
        break
    fi
done

# M8-B (Node deps). Mirror of run_m8_deps_case but for Node + npm.
# Build-time network required.
run_m8b_deps_case() {
    local hermetic_dir
    hermetic_dir="$(find "$RUNTIMES_CACHE/node" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)"
    log ""
    log "── nodejs-with-deps (M8-B: npm install into staged project) ──"
    if [[ -z "$hermetic_dir" || ! -x "$hermetic_dir/bin/node" ]]; then
        warn "vendored Node missing; run scripts/vendor-runtimes.sh node first"
        return 0
    fi

    local fdir="$FIXTURE_DIR/nodejs-with-deps"
    local cw="$WORK_DIR/nodejs-with-deps"
    mkdir -p "$cw/build" "$cw/run"
    info "fixture: $fdir (package.json → picocolors@1.1.1)"

    local ic_root="${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/install-cache/nodejs"
    rm -rf "$ic_root"

    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --output="$cw/build/app" \
            >"$cw/build.log" 2>&1; then
        fail "ubuilder build failed (see $cw/build.log)"
        sed 's/^/    /' "$cw/build.log" | tail -n 20
        return 1
    fi
    ok "bundle built ($(du -h "$cw/build/app" | cut -f1))"
    if ! grep -q "Install cache stored (nodejs/" "$cw/build.log"; then
        fail "build #1 did not store an install-cache entry (see $cw/build.log)"
        return 1
    fi
    ok "build #1 stored install-cache entry"

    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --output="$cw/build/app2" \
            >"$cw/build2.log" 2>&1; then
        fail "ubuilder build #2 failed (see $cw/build2.log)"
        sed 's/^/    /' "$cw/build2.log" | tail -n 20
        return 1
    fi
    if ! grep -q "Install cache hit (nodejs/" "$cw/build2.log"; then
        fail "build #2 missed the install cache (see $cw/build2.log)"
        return 1
    fi
    if grep -qE "^Installing dependencies from .* into staged project" "$cw/build2.log"; then
        fail "build #2 ran npm despite cache hit"
        return 1
    fi
    ok "build #2 hit install cache (no npm install run)"

    cp "$cw/build/app" "$cw/run/app"
    chmod +x "$cw/run/app"

    local fake_path; fake_path="$(mktemp -d)"
    local exit_code=0
    ( cd "$cw/run" && PATH="$fake_path" ./app "hello world" "it's" "ok" ) \
        >"$cw/stdout.txt" 2>"$cw/stderr.txt" || exit_code=$?
    rmdir "$fake_path"

    local rc=0
    if (( exit_code != 0 )); then
        fail "exit $exit_code (expected 0)"
        sed 's/^/    [stderr] /' "$cw/stderr.txt" | tail -n 10
        rc=1
    fi
    if ! diff -u "$fdir/expected.txt" "$cw/stdout.txt" >"$cw/stdout.diff" 2>&1; then
        fail "stdout differs"
        sed 's/^/    /' "$cw/stdout.diff" | head -n 30
        rc=1
    fi

    # Assert the user's project dir was NOT mutated (no node_modules/ left behind).
    if [[ -d "$fdir/node_modules" ]]; then
        fail "user project was polluted: $fdir/node_modules exists — staging did not isolate"
        rc=1
    fi

    if (( rc == 0 )); then
        ok "bundle imports picocolors (installed at build), runs PATH-stripped, user project untouched"
    fi
    return $rc
}

for rt in "${REQUESTED[@]}"; do
    if [[ "$rt" == "nodejs" ]]; then
        run_m8b_deps_case || ((failures++))
        break
    fi
done

# Node --exclude on a module: package.json with two deps, build with
# --exclude=is-number → filter rewrites the staged package.json before
# npm install runs; bundle uses picocolors but require("is-number") throws.
run_nodejs_exclude_case() {
    local hermetic_dir
    hermetic_dir="$(find "$RUNTIMES_CACHE/node" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)"
    log ""
    log "── nodejs-with-exclude (--exclude=<module> filters package.json) ──"
    if [[ -z "$hermetic_dir" || ! -x "$hermetic_dir/bin/node" ]]; then
        warn "vendored Node missing; skipping"
        return 0
    fi

    local fdir="$FIXTURE_DIR/nodejs-with-exclude"
    local cw="$WORK_DIR/nodejs-with-exclude"
    mkdir -p "$cw/build" "$cw/run"
    info "fixture: $fdir (deps: picocolors+is-number; --exclude=is-number)"

    rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/install-cache/nodejs"

    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --output="$cw/build/app" \
            --exclude=is-number \
            >"$cw/build.log" 2>&1; then
        fail "build failed"
        sed 's/^/    /' "$cw/build.log" | tail -n 20
        return 1
    fi
    if ! grep -q "Filtered 1 dep(s) from staged package.json via --exclude" "$cw/build.log"; then
        fail "build log missing 'Filtered 1 dep(s)' marker"
        sed 's/^/    /' "$cw/build.log" | tail -n 20
        return 1
    fi
    if ! grep -q "dependencies.is-number (excluded by config; not installed)" "$cw/build.log"; then
        fail "build log missing per-dep excluded notice"
        return 1
    fi
    local rc=0
    # User's source package.json must be untouched (we only edit the stage).
    # `pc_copy_or_link_tree` hardlinks when source/dest share a filesystem,
    # so a naive in-place rewrite would clobber the user's file. This is
    # the canary that catches that regression.
    if [[ ! -f "$fdir/package.json" ]] || ! grep -q "is-number" "$fdir/package.json"; then
        fail "user package.json was mutated by the build (staging promise broken)"
        rc=1
    fi
    if (( rc == 0 )); then
        ok "build filtered is-number out of staged package.json; source untouched"
    fi

    cp "$cw/build/app" "$cw/run/app"; chmod +x "$cw/run/app"
    local fake_path; fake_path="$(mktemp -d)"
    local exit_code=0
    ( cd "$cw/run" && PATH="$fake_path" ./app "hello world" "it's" "ok" ) \
        >"$cw/stdout.txt" 2>"$cw/stderr.txt" || exit_code=$?
    rmdir "$fake_path"

    if (( exit_code != 0 )); then
        fail "exit $exit_code (expected 0)"
        sed 's/^/    [stderr] /' "$cw/stderr.txt" | tail -n 10
        rc=1
    fi
    if ! diff -u "$fdir/expected.txt" "$cw/stdout.txt" >"$cw/stdout.diff" 2>&1; then
        fail "stdout differs"
        sed 's/^/    /' "$cw/stdout.diff" | head -n 20
        rc=1
    fi
    if (( rc == 0 )); then
        ok "bundle requires picocolors and proves is-number is absent at runtime"
    fi
    return $rc
}

for rt in "${REQUESTED[@]}"; do
    if [[ "$rt" == "nodejs" ]]; then
        run_nodejs_exclude_case || ((failures++))
        break
    fi
done

# M1-D (PHP deps). Mirror of run_m8b_deps_case but for PHP + composer.
# Unlike Python/Node we don't strip PATH — PHP is still host-bits hermetic
# (no upstream prebuilt static PHP), so the bundle's bin/php links against
# system libs and needs the host PHP shared-lib environment on $LD_LIBRARY_PATH.
# Build-time network required (composer install pulls from packagist).
run_m1d_php_deps_case() {
    log ""
    log "── php-with-deps (M1-D: composer install + install-cache) ──"
    if ! command -v php  >/dev/null; then warn "host php missing; skipping";  return 0; fi
    if ! command -v composer >/dev/null && ! command -v composer.phar >/dev/null; then
        warn "composer not on PATH; skipping (install from https://getcomposer.org/)"
        return 0
    fi

    local fdir="$FIXTURE_DIR/php-with-deps"
    local cw="$WORK_DIR/php-with-deps"
    mkdir -p "$cw/build" "$cw/run"
    info "fixture: $fdir (composer.json → psr/log ^1.1)"

    local ic_root="${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/install-cache/php"
    rm -rf "$ic_root"

    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --output="$cw/build/app" \
            >"$cw/build.log" 2>&1; then
        fail "ubuilder build failed (see $cw/build.log)"
        sed 's/^/    /' "$cw/build.log" | tail -n 20
        return 1
    fi
    ok "bundle built ($(du -h "$cw/build/app" | cut -f1))"
    if ! grep -q "Install cache stored (php/" "$cw/build.log"; then
        fail "build #1 did not store an install-cache entry (see $cw/build.log)"
        return 1
    fi
    ok "build #1 stored install-cache entry"

    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --output="$cw/build/app2" \
            >"$cw/build2.log" 2>&1; then
        fail "ubuilder build #2 failed (see $cw/build2.log)"
        sed 's/^/    /' "$cw/build2.log" | tail -n 20
        return 1
    fi
    if ! grep -q "Install cache hit (php/" "$cw/build2.log"; then
        fail "build #2 missed the install cache (see $cw/build2.log)"
        return 1
    fi
    if grep -qE "^Running composer install" "$cw/build2.log"; then
        fail "build #2 ran composer install despite cache hit"
        return 1
    fi
    ok "build #2 hit install cache (no composer install run)"

    cp "$cw/build/app" "$cw/run/app"
    chmod +x "$cw/run/app"

    local exit_code=0
    ( cd "$cw/run" && ./app "hello world" "it's" "ok" ) \
        >"$cw/stdout.txt" 2>"$cw/stderr.txt" || exit_code=$?

    local rc=0
    if (( exit_code != 0 )); then
        fail "exit $exit_code (expected 0)"
        sed 's/^/    [stderr] /' "$cw/stderr.txt" | tail -n 10
        rc=1
    fi
    if ! diff -u "$fdir/expected.txt" "$cw/stdout.txt" >"$cw/stdout.diff" 2>&1; then
        fail "stdout differs"
        sed 's/^/    /' "$cw/stdout.diff" | head -n 30
        rc=1
    fi

    # Staging promise: composer must run in the stage copy, never in $fdir.
    # If vendor/ shows up under the user's project dir the staging path
    # is broken and we'd be polluting the user's source tree.
    if [[ -e "$fdir/vendor" ]]; then
        fail "user project was polluted: $fdir/vendor exists — staging did not isolate"
        rc=1
    fi
    if [[ -e "$fdir/composer.lock" ]]; then
        fail "user project was polluted: $fdir/composer.lock written back — staging did not isolate"
        rc=1
    fi

    if (( rc == 0 )); then
        ok "bundle loads vendor/autoload.php, instantiates Psr\\Log\\NullLogger, user project untouched"
    fi
    return $rc
}

for rt in "${REQUESTED[@]}"; do
    if [[ "$rt" == "php" ]]; then
        run_m1d_php_deps_case || ((failures++))
        break
    fi
done

# M1-D negative path. If composer.json declares an `ext-*` that the host's
# extension_dir doesn't ship, php_stage_synthetic_runtime aborts with a
# precise error + apt/dnf install hint. Host-deterministic: we pick an
# obviously-fake name so this fires every time.
run_m1d_php_missing_ext_case() {
    log ""
    log "── php-missing-ext (M1-D: missing-extension error has install hint) ──"
    if ! command -v php >/dev/null; then warn "host php missing; skipping"; return 0; fi

    local fdir="$FIXTURE_DIR/php-missing-ext"
    local cw="$WORK_DIR/php-missing-ext"
    mkdir -p "$cw/build"
    info "fixture: $fdir (composer.json → ext-ubuilder_intentionally_missing)"

    local exit_code=0
    "$UBUILDER_BIN" \
        --project-dir="$fdir" \
        --output="$cw/build/app" \
        >"$cw/build.log" 2>&1 || exit_code=$?

    local rc=0
    if (( exit_code == 0 )); then
        fail "build succeeded but should have failed (declared ext is absent)"
        rc=1
    fi
    if ! grep -q "composer.json declares ext-ubuilder_intentionally_missing" "$cw/build.log"; then
        fail "build log missing the 'composer.json declares ext-X' line"
        sed 's/^/    /' "$cw/build.log" | tail -n 15
        rc=1
    fi
    # Install hint: Linux PHP host shows distro-specific apt + dnf lines;
    # statically linked host PHP (e.g. Laravel Herd, static-php-cli) can't
    # be fixed by apt-installing a `.so`, so the builder instead suggests
    # rebuilding static-php-cli with the missing extension. Either shape
    # is an acceptable hint as long as the missing ext name is named.
    if grep -q "sudo apt install php-ubuilder_intentionally_missing" "$cw/build.log" \
       && grep -q "sudo dnf install php-ubuilder_intentionally_missing" "$cw/build.log"; then
        : # Linux-style hint present
    elif grep -q "static-php-cli" "$cw/build.log" \
         && grep -q "ubuilder_intentionally_missing" "$cw/build.log"; then
        : # static-PHP-style hint present (macOS Herd, etc.)
    else
        fail "build log missing install hint for ext-ubuilder_intentionally_missing (expected apt/dnf or static-php-cli rebuild guidance)"
        sed 's/^/    /' "$cw/build.log" | tail -n 15
        rc=1
    fi
    # Stage cleanup promise: the staging dir under XDG_CACHE_HOME / HOME
    # must be removed by php_embed_runtime before returning. (We can't
    # know the exact pid-suffixed name, so we just check the parent dir
    # contains no `php-rt-*` leftovers from this test.)
    local stage_parent="${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/stage"
    if [[ -d "$stage_parent" ]] && find "$stage_parent" -maxdepth 1 -name 'php-rt-*' -type d 2>/dev/null | grep -q .; then
        fail "staging dir leaked at $stage_parent/php-rt-*"
        rc=1
    fi

    if (( rc == 0 )); then
        ok "build aborts with composer.json line + apt + dnf install hints; no staging leak"
    fi
    return $rc
}

for rt in "${REQUESTED[@]}"; do
    if [[ "$rt" == "php" ]]; then
        run_m1d_php_missing_ext_case || ((failures++))
        break
    fi
done

# Excluding a missing ext via --exclude should turn the previous case's
# hard failure into a successful build. Proves the exclude pipeline runs
# before php_stage_synthetic_runtime's missing-extension check.
run_m1d_php_exclude_ext_case() {
    log ""
    log "── php-exclude-ext (--exclude=ext-X drops composer-declared ext) ──"
    if ! command -v php >/dev/null; then warn "host php missing; skipping"; return 0; fi
    # The signal we assert below ("passing --ignore-platform-req=…") is only
    # printed inside the composer-install code path. When composer is not on
    # PATH (the macos-latest GH runner stopped shipping it), ubuilder takes
    # the "bundling project as-is" branch and never spawns composer, so the
    # flag never appears even when the exclude is wired correctly. Skip
    # rather than report a misleading failure.
    if ! command -v composer >/dev/null; then warn "composer not on PATH; skipping (install from https://getcomposer.org/)"; return 0; fi

    local fdir="$FIXTURE_DIR/php-missing-ext"
    local cw="$WORK_DIR/php-exclude-ext"
    mkdir -p "$cw/build"
    info "fixture: $fdir + --exclude=ext-ubuilder_intentionally_missing"

    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --exclude=ext-ubuilder_intentionally_missing \
            --output="$cw/build/app" \
            >"$cw/build.log" 2>&1; then
        fail "build failed even with --exclude for the missing ext"
        sed 's/^/    /' "$cw/build.log" | tail -n 15
        return 1
    fi
    # Two paths into the exclude pipeline:
    #   (a) ext exists on host → host-scan picks it up → exclude loop drops
    #       it with a per-name "(excluded by config; not staged)" line.
    #   (b) ext does NOT exist on host (this fixture) → host-scan never
    #       includes it, so the per-name line never fires. The signal that
    #       the exclude worked here is composer install receiving the
    #       --ignore-platform-req flag we built up below the cross-check.
    if ! grep -q "passing --ignore-platform-req=ext-ubuilder_intentionally_missing" "$cw/build.log"; then
        fail "build log missing the --ignore-platform-req flag for the excluded ext"
        sed 's/^/    /' "$cw/build.log" | tail -n 15
        return 1
    fi
    if grep -q "composer.json declares ext-ubuilder_intentionally_missing but no" "$cw/build.log"; then
        fail "exclude did not run before the missing-ext cross-check"
        return 1
    fi
    ok "build succeeds with --exclude; declared-but-absent ext is suppressed at composer install"
    return 0
}

for rt in "${REQUESTED[@]}"; do
    if [[ "$rt" == "php" ]]; then
        run_m1d_php_exclude_ext_case || ((failures++))
        break
    fi
done

# Auto-config-write: a first build with no ubuilder.json (only CLI flags)
# must write a ubuilder.json into the project dir capturing runtime,
# entry_point, and --exclude entries. A second build with no flags must
# pick that file up and succeed. File-glob exclusion is asserted via the
# CLI write-up — actually inspecting the bundle for the excluded path
# requires extracting the launcher, which is outside the harness's scope.
run_m1d_php_autoconfig_case() {
    log ""
    log "── php-autoconfig (auto-write ubuilder.json + --exclude file globs) ──"
    if ! command -v php >/dev/null; then warn "host php missing; skipping"; return 0; fi

    local src="$FIXTURE_DIR/php"
    local cw="$WORK_DIR/php-autoconfig"
    mkdir -p "$cw/build"
    cp -r "$src" "$cw/proj"
    rm "$cw/proj/ubuilder.json"           # "first build" — no config yet
    mkdir -p "$cw/proj/tests"
    echo "scratch" > "$cw/proj/tests/scratch.txt"
    echo "# doc" > "$cw/proj/README.md"

    info "first build via CLI flags only, expect auto-gen of ubuilder.json"
    if ! "$UBUILDER_BIN" \
            --project-dir="$cw/proj" \
            --runtime=php \
            --entry-point=main.php \
            --output="$cw/build/app" \
            --exclude='tests/**' \
            --exclude='*.md' \
            >"$cw/build1.log" 2>&1; then
        fail "first build failed"
        sed 's/^/    /' "$cw/build1.log" | tail -n 15
        return 1
    fi
    if [[ ! -f "$cw/proj/ubuilder.json" ]]; then
        fail "ubuilder.json was NOT written to the project dir"
        return 1
    fi
    if ! grep -q "wrote $cw/proj/ubuilder.json" "$cw/build1.log"; then
        fail "build log missing the 'wrote ubuilder.json' notice"
        return 1
    fi
    if ! grep -q '"runtime": "php"' "$cw/proj/ubuilder.json"; then
        fail "auto ubuilder.json missing runtime entry"
        cat "$cw/proj/ubuilder.json"
        return 1
    fi
    if ! grep -q '"entry_point": "main.php"' "$cw/proj/ubuilder.json"; then
        fail "auto ubuilder.json missing entry_point"
        cat "$cw/proj/ubuilder.json"
        return 1
    fi
    if ! grep -q 'tests/' "$cw/proj/ubuilder.json" || ! grep -q '\.md' "$cw/proj/ubuilder.json"; then
        fail "auto ubuilder.json missing exclude entries"
        cat "$cw/proj/ubuilder.json"
        return 1
    fi
    ok "first build wrote ubuilder.json with runtime, entry_point, and exclude"

    info "second build with no CLI flags, expect config pickup"
    if ! "$UBUILDER_BIN" \
            --project-dir="$cw/proj" \
            --output="$cw/build/app2" \
            >"$cw/build2.log" 2>&1; then
        fail "second build (config-only) failed"
        sed 's/^/    /' "$cw/build2.log" | tail -n 15
        return 1
    fi
    # And ubuilder.json should NOT be rewritten on the second build —
    # auto-write is first-build-only.
    if grep -q "wrote $cw/proj/ubuilder.json" "$cw/build2.log"; then
        fail "second build overwrote ubuilder.json (should be no-op when present)"
        return 1
    fi
    ok "second build reuses the auto-written config and does not overwrite it"
    return 0
}

for rt in "${REQUESTED[@]}"; do
    if [[ "$rt" == "php" ]]; then
        run_m1d_php_autoconfig_case || ((failures++))
        break
    fi
done

log ""
if (( failures == 0 )); then
    log "${C_GRN}all bundle tests passed${C_RST}"
    exit 0
fi
log "${C_RED}$failures bundle test(s) failed${C_RST}"
(( failures > 125 )) && failures=125
exit "$failures"
