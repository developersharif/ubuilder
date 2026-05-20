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

# Launch the bundle just long enough for it to extract its runtime tree
# to a deterministic location, then snapshot the extracted tree before
# the bundle wipes it on exit.
WORK="$(mktemp -d -t ubuilder-portcheck.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

# Bundle extracts to /var/folders/.../T/ubuilder-<pid>/. We run the bundle
# under a short timeout against /dev/null stdin; if it exits quickly
# (cli) we snapshot the temp dir before cleanup; if it stays running
# (gui) we kill it.
LOG="$WORK/run.log"
echo "[port-check] starting bundle $BUNDLE, TMPDIR=${TMPDIR:-(unset)}"
"$BUNDLE" >"$LOG" 2>&1 &
PID=$!
echo "[port-check] launched PID=$PID"
EXTRACT=""
# Search wherever mkdtemp may have placed the launcher's extract dir.
# - $TMPDIR honored on macOS, GH Actions sets a runner-specific path.
# - /var/folders/*/*/T/ is the default user temp on macOS (M-series + Intel).
# - /tmp is the POSIX fallback the launcher uses when neither of the above
#   are usable.
# Use :- defaults so an unset TMPDIR doesn't trigger set -u.
for i in $(seq 1 50); do
    sleep 0.1
    EXTRACT="$(ls -dt "${TMPDIR:-/tmp}/ubuilder-${PID}" \
                       /var/folders/*/*/T/ubuilder-${PID} \
                       /tmp/ubuilder-${PID} \
                       2>/dev/null | head -1 || true)"
    [[ -n "$EXTRACT" && -d "$EXTRACT/runtime/bin" ]] && break
done
echo "[port-check] EXTRACT=${EXTRACT:-(not found)}"

# Wait for extraction to actually finish before SIGSTOP'ing. The launcher
# extracts the whole runtime tree (bin/php, lib/*.dylib, ext/, app/) into
# $EXTRACT THEN spawns the child interpreter. So the moment the launcher
# has a child process, extraction is complete. We poll for that — it's
# the only signal that fires AFTER extraction but BEFORE the child can
# finish and trigger the launcher's atexit cleanup.
extraction_done=0
for i in $(seq 1 50); do
    if pgrep -P "$PID" >/dev/null 2>&1; then
        extraction_done=1
        break
    fi
    # Bundle might have already finished + cleaned up (very fast CLI).
    # If the extract dir is gone, give up on the "wait for child" path
    # and just snapshot what we have (likely nothing).
    [[ ! -d "$EXTRACT" ]] && break
    sleep 0.05
done
echo "[port-check] extraction_done=$extraction_done"

# Freeze the launcher AND its child so neither can clean up while we
# snapshot. SIGSTOP is honored regardless of signal handlers, so this
# is racy-safe even against a launcher that traps SIGTERM.
kill -STOP "$PID" 2>/dev/null || true
pgrep -P "$PID" 2>/dev/null | xargs kill -STOP 2>/dev/null || true

# Mirror the entire extracted tree (runtime/ + app files like vendor/)
# before the bundle deletes it. The launcher chdir()s into the extract
# dir, so any dylibs loaded by the app via FFI (e.g. php-gui's Tcl/Tk)
# live at the bundle root, not under runtime/.
file_count=0
if [[ -n "$EXTRACT" && -d "$EXTRACT" ]]; then
    cp -R "$EXTRACT" "$WORK/extracted"
    file_count="$(find "$WORK/extracted" -type f 2>/dev/null | wc -l | tr -d ' ' || echo 0)"
fi
echo "[port-check] snapshotted $file_count file(s)"

# Now resume + terminate the bundle.
kill -CONT "$PID" 2>/dev/null || true
pgrep -P "$PID" 2>/dev/null | xargs kill -CONT 2>/dev/null || true
kill -TERM "$PID" 2>/dev/null || true
pkill -P "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true

if [[ ! -d "$WORK/extracted" ]]; then
    echo "error: could not snapshot extracted tree" >&2
    echo "  PID:     $PID" >&2
    echo "  TMPDIR:  ${TMPDIR:-(unset)}" >&2
    echo "  EXTRACT: ${EXTRACT:-(empty)}" >&2
    echo "  --- candidate temp dirs explored ---" >&2
    ls -la "${TMPDIR%/}/" /var/folders/*/*/T/ /tmp/ 2>/dev/null | grep -E "ubuilder|^total" | head -20 >&2 || true
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
