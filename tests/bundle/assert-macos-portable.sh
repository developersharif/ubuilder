#!/usr/bin/env bash
# assert-macos-portable.sh — verify a built ubuilder bundle is portable
# across Macs. Walks the bundle's extracted runtime tree, runs `otool -L`
# on every Mach-O, and asserts that every LC_LOAD_DYLIB reference is one
# of:
#   - /usr/lib/*           (macOS base install, guaranteed present)
#   - /System/*            (system frameworks, guaranteed present)
#   - @executable_path/... (bundle-relative)
#   - @loader_path/...     (loader-relative, intra-bundle)
#   - @rpath/...           (resolved via the binary's LC_RPATH list — out
#                          of scope for v1, but harmless if all LC_RPATH
#                          entries are themselves @executable_path-rel)
#
# Any reference starting with /opt/, /usr/local/, $HOME, or any other
# absolute path outside /usr/lib and /System indicates a portability bug
# — the bundle would fail to launch on a Mac that doesn't have that path.
#
# Usage:
#   tests/bundle/assert-macos-portable.sh <bundle-path>
#
# Exits 0 if portable, non-zero with details otherwise.

set -euo pipefail

if [[ "$(uname)" != "Darwin" ]]; then
    echo "[skip] not on macOS"
    exit 0
fi

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <bundle-path>" >&2
    exit 2
fi

BUNDLE="$1"
if [[ ! -x "$BUNDLE" ]]; then
    echo "error: $BUNDLE is not an executable file" >&2
    exit 2
fi

# Run the bundle with UBUILDER_KEEP_EXTRACT=1 so the launcher skips its
# atexit cleanup and leaves the extracted tree on disk for us to inspect.
# This sidesteps the race that biased earlier versions of this script:
# a fast CLI fixture (PHP "hello world" finishes in <100ms on M-series)
# would extract, run, AND wipe the temp dir before the first 100ms poll
# could find it. With keep-extract, the launcher logs the dir path to
# stderr; we parse it and snapshot AFTER the bundle exits cleanly.
WORK="$(mktemp -d -t ubuilder-portcheck.XXXXXX)"
EXTRACT=""
cleanup() {
    rm -rf "$WORK"
    [[ -n "$EXTRACT" && -d "$EXTRACT" ]] && rm -rf "$EXTRACT"
}
trap cleanup EXIT

LOG="$WORK/run.log"
echo "[port-check] starting bundle $BUNDLE, TMPDIR=${TMPDIR:-(unset)}"

# Background-run + wait. CI bundles finish in ~1s; local dev machines
# can take 10s+ for first-launch dyld + codesign quarantine validation
# on macOS Sequoia, so the watchdog is generous (60s). It exists only
# to defend against a hypothetical GUI fixture that holds the launcher
# open indefinitely — not to time normal CLI bundles.
UBUILDER_KEEP_EXTRACT=1 "$BUNDLE" >"$LOG" 2>&1 &
BPID=$!
( sleep 60 && kill -KILL "$BPID" 2>/dev/null ) &
WPID=$!
wait "$BPID" 2>/dev/null || true
kill "$WPID" 2>/dev/null || true
wait "$WPID" 2>/dev/null || true
echo "[port-check] bundle exited"

# Launcher prints `[ubuilder] UBUILDER_KEEP_EXTRACT set; preserving <path>`
# to stderr when keep-extract is honored. Pull the path out of the log.
EXTRACT="$(grep -E '^\[ubuilder\] UBUILDER_KEEP_EXTRACT set; preserving ' "$LOG" \
            | sed -E 's/^\[ubuilder\] UBUILDER_KEEP_EXTRACT set; preserving //' \
            | tail -1 || true)"
echo "[port-check] EXTRACT=${EXTRACT:-(not found)}"

file_count=0
if [[ -n "$EXTRACT" && -d "$EXTRACT" ]]; then
    cp -R "$EXTRACT" "$WORK/extracted"
    file_count="$(find "$WORK/extracted" -type f 2>/dev/null | wc -l | tr -d ' ' || echo 0)"
fi
echo "[port-check] snapshotted $file_count file(s)"

if [[ ! -d "$WORK/extracted" ]]; then
    echo "error: could not snapshot extracted tree" >&2
    echo "  TMPDIR:  ${TMPDIR:-(unset)}" >&2
    echo "  EXTRACT: ${EXTRACT:-(empty)}" >&2
    echo "  --- bundle stdout/stderr ---" >&2
    cat "$LOG" >&2 || true
    exit 1
fi

# Walk every regular file under the extracted tree; for each one, ask
# `file` if it's a Mach-O. Then `otool -L` it and assert every dep is
# portable.
violations=0
checked=0
while IFS= read -r -d '' f; do
    [[ -L "$f" ]] && continue
    kind="$(file -b "$f" 2>/dev/null || true)"
    case "$kind" in
        Mach-O*) ;;
        *) continue ;;
    esac
    checked=$((checked + 1))
    if [[ -n "${PORTCHECK_VERBOSE:-}" ]]; then
        rel="${f#$WORK/extracted/}"
        echo "  [scan] $rel"
    fi

    while IFS= read -r dep; do
        # otool -L lines look like:   \tPATH (compatibility ..., current ...)
        path="$(echo "$dep" | sed -E 's/^[[:space:]]*//; s/[[:space:]]+\(compatibility.*$//')"
        [[ -z "$path" ]] && continue
        case "$path" in
            /usr/lib/*|/System/*|@executable_path/*|@loader_path/*|@rpath/*)
                ;;
            *)
                rel="${f#$WORK/extracted/}"
                echo "  ❌ $rel"
                echo "       references non-portable path: $path"
                violations=$((violations + 1))
                ;;
        esac
    # Skip the first non-empty line (it's the file header, ends with ':').
    done < <(otool -L "$f" 2>/dev/null | tail -n +2)
done < <(find "$WORK/extracted" -type f -print0)

echo "[port-check] inspected $checked Mach-O file(s)"
if (( violations > 0 )); then
    echo "[port-check] ❌ $violations non-portable reference(s) found"
    exit 1
fi
echo "[port-check] ✅ all references are @executable_path-relative or system (/usr/lib, /System)"
exit 0
