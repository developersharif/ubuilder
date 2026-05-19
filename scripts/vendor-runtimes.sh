#!/usr/bin/env bash
# scripts/vendor-runtimes.sh
#
# Download hermetic interpreters for UBuilder's M1 path (audit §4.2).
# Downloads are pinned by version + SHA-256; cache lives under
# $UBUILDER_RUNTIMES_CACHE (default: $XDG_CACHE_HOME/ubuilder/runtimes/
# or ~/.cache/ubuilder/runtimes/).
#
# Usage:
#   scripts/vendor-runtimes.sh                    # fetch all default runtimes
#   scripts/vendor-runtimes.sh python             # specific runtime(s)
#   UBUILDER_RUNTIMES_CACHE=/path … vendor-runtimes.sh python
#
# Honors the Apple-sandbox rule: every download is checksum-verified before
# extraction; extraction failure leaves no partial artefact in the cache.

set -euo pipefail

CACHE_ROOT="${UBUILDER_RUNTIMES_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/ubuilder/runtimes}"

# Quiet mode. Triggered by UBUILDER_AUTO_VENDOR=1, which ubuilder sets
# when auto-spawning the script. Interactive callers see the full chatter
# unless they pass UBUILDER_VENDOR_QUIET=1; auto-callers can re-enable
# verbose output with UBUILDER_VENDOR_VERBOSE=1.
if [[ "${UBUILDER_VENDOR_VERBOSE:-}" == "1" ]]; then
    QUIET=0
elif [[ "${UBUILDER_VENDOR_QUIET:-}" == "1" || "${UBUILDER_AUTO_VENDOR:-}" == "1" ]]; then
    QUIET=1
else
    QUIET=0
fi
if (( QUIET )); then
    CURL_ARGS=(--fail --location --retry 3 --silent --show-error)
else
    CURL_ARGS=(--fail --location --retry 3 --progress-bar)
fi
say() { (( QUIET )) || printf '%s\n' "$*"; }

# Detect platform/arch for tag construction.
case "$(uname -s)" in
    Linux)  PLATFORM=linux ;;
    Darwin) PLATFORM=macos ;;
    *)      echo "unsupported OS: $(uname -s)" >&2; exit 2 ;;
esac
case "$(uname -m)" in
    x86_64|amd64) ARCH=x86_64 ;;
    aarch64|arm64) ARCH=aarch64 ;;
    *) echo "unsupported arch: $(uname -m)" >&2; exit 2 ;;
esac

# ----- runtime manifest --------------------------------------------------
# Each entry is: <runtime>|<version-tag>|<url>|<sha256>|<extracted-dir-name>
# Linux x86_64, macOS arm64, macOS x86_64. Windows is wired by main.c's
# native path (no runtime tarball needed). python-build-standalone tags
# publish artefacts for all four; node.js dist publishes per-arch tarballs.

PYTHON_VER="3.12.13+20260510"
PYTHON_URL_LINUX_X64="https://github.com/astral-sh/python-build-standalone/releases/download/20260510/cpython-${PYTHON_VER}-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz"
PYTHON_SHA_LINUX_X64="d480f5d5878910ecbae212bf23bd7c25d7b209eb8cf5e98823c977384d272e88"
PYTHON_URL_MACOS_ARM64="https://github.com/astral-sh/python-build-standalone/releases/download/20260510/cpython-${PYTHON_VER}-aarch64-apple-darwin-install_only_stripped.tar.gz"
PYTHON_SHA_MACOS_ARM64="55bc1a5edbc8ac4da0081f4f5731ed2d1ed10c57cb37a820b2a0dbc7cad742e9"
PYTHON_URL_MACOS_X64="https://github.com/astral-sh/python-build-standalone/releases/download/20260510/cpython-${PYTHON_VER}-x86_64-apple-darwin-install_only_stripped.tar.gz"
PYTHON_SHA_MACOS_X64="6bab7fa97d4f2ddba86da0e05acff66c53b5edaca1df8edcf00ddca785a9c59b"

# Node.js: official nodejs.org prebuilt. Linux requires glibc 2.28+ on the
# target (Ubuntu 20.04+, RHEL 8+, Debian 10+). For older targets, swap in
# a musl build from https://unofficial-builds.nodejs.org/ via --runtime-source.
NODE_VER="24.15.0"
NODE_URL_LINUX_X64="https://nodejs.org/dist/v${NODE_VER}/node-v${NODE_VER}-linux-x64.tar.xz"
NODE_SHA_LINUX_X64="472655581fb851559730c48763e0c9d3bc25975c59d518003fc0849d3e4ba0f6"
NODE_URL_MACOS_ARM64="https://nodejs.org/dist/v${NODE_VER}/node-v${NODE_VER}-darwin-arm64.tar.xz"
NODE_SHA_MACOS_ARM64="af5cfaeafe603aaf7599f287fd9d100bb41f16794f49788fa59dd3f25546930f"
NODE_URL_MACOS_X64="https://nodejs.org/dist/v${NODE_VER}/node-v${NODE_VER}-darwin-x64.tar.xz"
NODE_SHA_MACOS_X64="5d627245b9f53cb2512cc21b7aa6aad693106affadd91e0c8f42d600fb7ba444"

# PHP: no upstream pre-built static PHP exists today. static-php-cli
# (https://github.com/crazywhalecc/static-php-cli) lets you self-build a
# single static PHP binary in ~10 min. Until we automate that here, PHP
# stays in non-hermetic (host) mode unless you point --runtime-source at
# a binary you built. See docs/architecture/M1_HERMETIC_INTERPRETERS.md.

manifest() {
    # Stdout: "<key>|<url>|<sha256>|<cache-subdir>"
    if [[ "$PLATFORM" == "linux" && "$ARCH" == "x86_64" ]]; then
        printf 'python|%s|%s|python/%s\n' "$PYTHON_URL_LINUX_X64" "$PYTHON_SHA_LINUX_X64" "$PYTHON_VER"
        printf 'node|%s|%s|node/%s\n'     "$NODE_URL_LINUX_X64"   "$NODE_SHA_LINUX_X64"   "$NODE_VER"
    elif [[ "$PLATFORM" == "macos" && "$ARCH" == "aarch64" ]]; then
        printf 'python|%s|%s|python/%s\n' "$PYTHON_URL_MACOS_ARM64" "$PYTHON_SHA_MACOS_ARM64" "$PYTHON_VER"
        printf 'node|%s|%s|node/%s\n'     "$NODE_URL_MACOS_ARM64"   "$NODE_SHA_MACOS_ARM64"   "$NODE_VER"
    elif [[ "$PLATFORM" == "macos" && "$ARCH" == "x86_64" ]]; then
        printf 'python|%s|%s|python/%s\n' "$PYTHON_URL_MACOS_X64" "$PYTHON_SHA_MACOS_X64" "$PYTHON_VER"
        printf 'node|%s|%s|node/%s\n'     "$NODE_URL_MACOS_X64"   "$NODE_SHA_MACOS_X64"   "$NODE_VER"
    fi
}

# ----- helpers -----------------------------------------------------------

verify_sha() {
    # $1 = file, $2 = expected sha256 (hex)
    local got
    got="$(sha256sum "$1" | awk '{print $1}')"
    if [[ "$got" != "$2" ]]; then
        printf 'sha256 mismatch for %s\n  expected: %s\n  got:      %s\n' \
            "$1" "$2" "$got" >&2
        return 1
    fi
}

download_one() {
    local runtime="$1" url="$2" sha="$3" cache_subdir="$4"
    local dest="$CACHE_ROOT/$cache_subdir"
    local marker="$dest/.vendor-ok"

    if [[ -f "$marker" ]]; then
        say "  $runtime already present at $dest"
        return 0
    fi

    local tmpdir
    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' RETURN

    local tarball="$tmpdir/runtime.tarball"
    say "  downloading $url"
    curl "${CURL_ARGS[@]}" -o "$tarball" "$url"

    say "  verifying sha256"
    verify_sha "$tarball" "$sha"

    say "  extracting"
    mkdir -p "$dest"
    tar -xf "$tarball" -C "$tmpdir"
    local extracted
    extracted="$(find "$tmpdir" -mindepth 1 -maxdepth 1 -type d ! -name "$(basename "$tmpdir")" | head -1)"
    if [[ -z "$extracted" ]]; then
        echo "extraction produced no directory" >&2
        return 1
    fi
    cp -a "$extracted"/. "$dest"/
    touch "$marker"
    say "  vendored $runtime -> $dest"
}

# ----- main --------------------------------------------------------------

if (($# == 0)); then
    REQUESTED=("python")    # default set
else
    REQUESTED=("$@")
fi

mkdir -p "$CACHE_ROOT"

say "vendoring runtimes into $CACHE_ROOT (platform=$PLATFORM arch=$ARCH)"

# Track which runtimes the manifest knows about, in a bash-3.2-compatible
# way (macOS ships bash 3.2 by default; `declare -A` is bash-4+). We use a
# space-padded string + glob match for membership testing instead.
SEEN=" "
while IFS='|' read -r runtime url sha subdir; do
    SEEN="$SEEN$runtime "
    want=0
    for r in "${REQUESTED[@]}"; do
        if [[ "$r" == "$runtime" || "$r" == "all" ]]; then want=1; break; fi
    done
    (( want )) || continue
    say ""
    say "=== $runtime ==="
    download_one "$runtime" "$url" "$sha" "$subdir"
done < <(manifest)

# Surface unsupported requests loudly (always — even in quiet mode).
for r in "${REQUESTED[@]}"; do
    [[ "$r" == "all" ]] && continue
    case "$SEEN" in
        *" $r "*) ;;
        *) printf '\nwarning: no vendor entry for %s on %s/%s yet\n' "$r" "$PLATFORM" "$ARCH" >&2 ;;
    esac
done

say ""
say "done."
