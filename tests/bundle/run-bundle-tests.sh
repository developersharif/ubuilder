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

    # 5. Tamper test (S3 — Apple-sandbox integrity rule).
    #    Flip one byte in the middle of the file, which for our multi-MB
    #    bundles is always inside the payload region (the launcher binary
    #    is < 1 MB; the trailer is < 100 bytes at the tail). Post-S3
    #    bundles MUST refuse to run with non-zero exit and a clear
    #    "integrity check FAILED" diagnostic.
    if (( rc == 0 )); then
        local tamper_bin="$run_dir/app.tampered"
        cp "$run_dir/app" "$tamper_bin"
        local file_size; file_size="$(stat -c %s "$tamper_bin")"
        local mid=$(( file_size / 2 ))
        printf '\xAA' | dd of="$tamper_bin" bs=1 seek="$mid" count=1 conv=notrunc \
            >/dev/null 2>&1
        chmod +x "$tamper_bin"
        local tamper_exit=0
        ( "$tamper_bin" >/dev/null 2>"$case_work/tamper.stderr" ) || tamper_exit=$?
        if (( tamper_exit == 0 )); then
            fail "tampered bundle ran successfully — integrity check did not fire"
            rc=1
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

log ""
if (( failures == 0 )); then
    log "${C_GRN}all bundle tests passed${C_RST}"
    exit 0
fi
log "${C_RED}$failures bundle test(s) failed${C_RST}"
(( failures > 125 )) && failures=125
exit "$failures"
