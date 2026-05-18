#!/usr/bin/env bash
# tests/bundle/run-tier3.sh
#
# Tier-3 hermeticity test (audit §6, M1-C):
#   Build a hermetic bundle and run it in a sandboxed environment that has
#   no host Python/PHP/Node installed *anywhere on the filesystem* — not
#   just absent from PATH. If the bundle still produces the expected
#   output, the "true zero-dependency executable" claim holds.
#
# Three isolation strategies, tried in this order:
#   1. Docker — preferred in CI; uses debian:12-slim (glibc, no language
#      runtimes preinstalled). Most thorough.
#   2. Bubblewrap (bwrap) — preferred for local dev; faster, no daemon.
#      Bind-mounts /lib + /lib64 read-only (so dynamic linking still works
#      for the launcher) but presents an empty /usr/bin (no python3 / node
#      / php anywhere binaries are normally found).
#   3. Bare PATH-strip fallback — same as the regular harness; only a weak
#      proof. Used only when neither container runtime is available.
#
# Usage:
#   tests/bundle/run-tier3.sh                    # all runtimes that vendored
#   tests/bundle/run-tier3.sh python             # filter
#   UBUILDER_TIER3_MODE=docker tests/bundle/run-tier3.sh    # force a backend
#   UBUILDER_TIER3_MODE=bwrap  ...
#
# Exit code is the number of failed cases.

set -u
[[ "${UBUILDER_VERBOSE:-0}" == "1" ]] && set -x

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${UBUILDER_BUILD_DIR:-$REPO_ROOT/build}"
UBUILDER_BIN="${UBUILDER_BIN:-$BUILD_DIR/src/ubuilder}"
FIXTURE_DIR="$SCRIPT_DIR/fixtures"
# Work dir under $HOME because Docker Desktop only shares HOME by default;
# /tmp bind mounts fail with "Mounts denied: path is not shared from the
# host". Plain Docker (CI) and bwrap accept either path; HOME-rooted works
# everywhere.
TIER3_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/tier3-work"
mkdir -p "$TIER3_ROOT"
WORK_DIR="$(mktemp -d "$TIER3_ROOT/run.XXXXXX")"
RUNTIMES_CACHE="${UBUILDER_RUNTIMES_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/runtimes}"
DOCKER_IMAGE="${UBUILDER_TIER3_IMAGE:-debian:12-slim}"

if [[ -t 1 ]]; then
    C_RED='\033[0;31m'; C_GRN='\033[0;32m'; C_YEL='\033[1;33m'; C_DIM='\033[2m'; C_RST='\033[0m'
else
    C_RED=''; C_GRN=''; C_YEL=''; C_DIM=''; C_RST=''
fi
log()  { printf '%b\n' "$*"; }
info() { log "${C_DIM}[tier3]${C_RST} $*"; }
ok()   { log "${C_GRN}  ✓${C_RST} $*"; }
fail() { log "${C_RED}  ✗${C_RST} $*"; }
warn() { log "${C_YEL}  !${C_RST} $*"; }

cleanup() {
    if [[ "${UBUILDER_KEEP_ARTIFACTS:-0}" != "1" ]]; then
        rm -rf "$WORK_DIR"
    else
        info "keeping artifacts under $WORK_DIR"
    fi
}
trap cleanup EXIT

# ---- backend detection ---------------------------------------------------

pick_backend() {
    local forced="${UBUILDER_TIER3_MODE:-}"
    if [[ -n "$forced" ]]; then echo "$forced"; return; fi

    if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
        echo "docker"; return
    fi
    if command -v bwrap >/dev/null 2>&1; then
        echo "bwrap"; return
    fi
    echo "path-strip"
}

# ---- runtime registry ----------------------------------------------------

# Each entry: <runtime-key> <cache-subdir> <bin-rel-path>
# PHP not listed — no upstream pre-built static PHP exists (see M1 spec).

runtime_meta() {
    case "$1" in
        python) echo "python bin/python3" ;;
        nodejs) echo "node   bin/node"    ;;
        *)      return 1 ;;
    esac
}

# ---- per-backend runner --------------------------------------------------

run_in_docker() {
    local bundle="$1" stdout="$2" stderr="$3"
    # No bind mounts, no `docker cp` — both require Docker Desktop's File
    # Sharing list to include the source path. Instead, stream the bundle
    # into the container via stdin and have a tiny shell wrapper inside
    # write it to disk, mark it executable, and exec it. This works on
    # plain Docker (CI) and Docker Desktop (restrictive sharing) alike.
    #
    # The wrapper closes its own stdin after the cat completes by reading
    # from /dev/null, so the bundle itself doesn't see leftover bytes.
    docker run --rm -i \
        --entrypoint /bin/sh \
        "$DOCKER_IMAGE" \
        -c 'set -e
            cat > /tmp/bundle
            chmod +x /tmp/bundle
            exec </dev/null /tmp/bundle "$@"' \
        -- "hello world" "it's" "ok" \
        < "$bundle" >"$stdout" 2>"$stderr"
}

run_in_bwrap() {
    local bundle="$1" stdout="$2" stderr="$3"
    local mount_dir
    mount_dir="$(dirname "$bundle")"
    local bin_name
    bin_name="$(basename "$bundle")"
    # Construct an empty /usr/bin/ /sbin/ /bin/ via tmpfs overlay so no
    # host python/node/php can be discovered ANYWHERE; preserve /lib and
    # /lib64 (read-only) so the launcher's dynamic loader still resolves;
    # /tmp is a writable tmpfs for extraction.
    local empty_dir="$WORK_DIR/empty-bin"
    mkdir -p "$empty_dir"

    bwrap \
        --ro-bind /lib    /lib    \
        --ro-bind /lib64  /lib64  \
        --ro-bind /usr/lib /usr/lib \
        --ro-bind /usr/lib64 /usr/lib64 \
        --tmpfs   /tmp           \
        --proc    /proc          \
        --dev     /dev           \
        --bind    "$mount_dir" /work \
        --ro-bind "$empty_dir" /usr/bin \
        --ro-bind "$empty_dir" /usr/local/bin \
        --ro-bind "$empty_dir" /bin           \
        --ro-bind "$empty_dir" /sbin          \
        --chdir /work \
        --setenv PATH "/no/such/path" \
        --setenv HOME /tmp \
        --unshare-all --share-net \
        "/work/$bin_name" "hello world" "it's" "ok" \
        >"$stdout" 2>"$stderr"
}

run_with_path_strip() {
    local bundle="$1" stdout="$2" stderr="$3"
    local fake_path; fake_path="$(mktemp -d)"
    ( cd "$(dirname "$bundle")" && PATH="$fake_path" "./$(basename "$bundle")" "hello world" "it's" "ok" ) \
        >"$stdout" 2>"$stderr"
    rmdir "$fake_path"
}

# ---- one case ------------------------------------------------------------

# Args: rt cache_subdir rel_exe backend
run_one() {
    local rt="$1" cache_subdir="$2" rel_exe="$3" backend="$4"
    local fdir="$FIXTURE_DIR/$rt"
    local cw="$WORK_DIR/$rt"
    mkdir -p "$cw"

    local hermetic_dir
    hermetic_dir="$(find "$RUNTIMES_CACHE/$cache_subdir" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | head -1)"
    log ""
    log "── $rt (Tier-3 via $backend) ──"
    if [[ -z "$hermetic_dir" || ! -x "$hermetic_dir/$rel_exe" ]]; then
        warn "vendored $rt not present; run scripts/vendor-runtimes.sh $cache_subdir first"
        return 0
    fi

    info "vendored: $hermetic_dir"
    if ! "$UBUILDER_BIN" \
            --project-dir="$fdir" \
            --runtime-source="$hermetic_dir" \
            --output="$cw/app" \
            >"$cw/build.log" 2>&1; then
        fail "ubuilder build failed (see $cw/build.log)"
        sed 's/^/    /' "$cw/build.log" | tail -n 20
        return 1
    fi
    chmod +x "$cw/app"
    ok "bundle built ($(du -h "$cw/app" | cut -f1))"

    local stdout="$cw/stdout.txt" stderr="$cw/stderr.txt"
    local exit_code=0
    case "$backend" in
        docker)     run_in_docker    "$cw/app" "$stdout" "$stderr" || exit_code=$? ;;
        bwrap)      run_in_bwrap     "$cw/app" "$stdout" "$stderr" || exit_code=$? ;;
        path-strip) run_with_path_strip "$cw/app" "$stdout" "$stderr" || exit_code=$? ;;
        *) fail "unknown backend: $backend"; return 1 ;;
    esac

    local rc=0
    if (( exit_code != 0 )); then
        fail "bundle exited $exit_code (expected 0)"
        sed 's/^/    [stderr] /' "$stderr" | tail -n 15
        rc=1
    fi
    if ! diff -u "$fdir/expected.txt" "$stdout" >"$cw/stdout.diff" 2>&1; then
        fail "stdout differs from expected"
        sed 's/^/    /' "$cw/stdout.diff" | head -n 30
        rc=1
    fi
    if (( rc == 0 )); then
        case "$backend" in
            docker)     ok "bundle runs in $DOCKER_IMAGE (no host runtimes) — TIER-3 PASS" ;;
            bwrap)      ok "bundle runs in bwrap sandbox (empty /usr/bin) — TIER-3 PASS" ;;
            path-strip) ok "bundle runs with PATH stripped (weak proof) — Tier-3 (degraded)" ;;
        esac
    fi
    return $rc
}

# ---- main ----------------------------------------------------------------

if (($# == 0)); then
    REQUESTED=(python nodejs)
else
    REQUESTED=("$@")
fi

BACKEND="$(pick_backend)"
info "repo:    $REPO_ROOT"
info "backend: $BACKEND"
if [[ "$BACKEND" == "path-strip" ]]; then
    warn "neither docker nor bwrap available — falling back to PATH-strip"
    warn "this is a weaker proof; install bubblewrap or run with Docker for true Tier-3"
fi

if [[ ! -x "$UBUILDER_BIN" ]]; then
    fail "ubuilder not built at $UBUILDER_BIN"
    exit 2
fi

failures=0
for rt in "${REQUESTED[@]}"; do
    meta="$(runtime_meta "$rt" || true)"
    if [[ -z "$meta" ]]; then
        warn "no Tier-3 entry for runtime '$rt' (PHP intentionally skipped — no upstream static PHP)"
        continue
    fi
    # shellcheck disable=SC2086
    set -- $meta
    run_one "$rt" "$1" "$2" "$BACKEND" || ((failures++))
done

log ""
if (( failures == 0 )); then
    log "${C_GRN}tier-3 passed${C_RST}"
    exit 0
fi
log "${C_RED}$failures tier-3 case(s) failed${C_RST}"
(( failures > 125 )) && failures=125
exit "$failures"
