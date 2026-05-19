#!/bin/bash
# UBuilder Release Creation Script
#
# Usage:
#   scripts/create-release.sh <version> [message]              # cut a real release
#   scripts/create-release.sh <version> [message] --dry-run    # show what would happen
#
# Examples:
#   scripts/create-release.sh v2.2.0 "self-update + quiet output + ..."
#   scripts/create-release.sh v2.0.2 "Bug fixes" --dry-run

set -e

# Resolve repo root and cd there so the path-relative seds against
# CMakeLists.txt / src/core/ubuilder.h / CHANGELOG.md all work whether
# you invoked us as ./scripts/create-release.sh, ../scripts/..., etc.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status()  { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error()   { echo -e "${RED}[ERROR]${NC} $1" >&2; }

# ---------- arg parsing (positional + --dry-run anywhere) ----------
DRY_RUN=0
POSARGS=()
for arg in "$@"; do
    case "$arg" in
        --dry-run|-n) DRY_RUN=1 ;;
        -h|--help)
            sed -n '1,11p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) POSARGS+=("$arg") ;;
    esac
done
set -- "${POSARGS[@]}"

if [ $# -lt 1 ]; then
    print_error "Usage: $0 <version> [message] [--dry-run]"
    print_error "Example: $0 v2.1.0 \"exclude feature + auto-config\""
    exit 1
fi

VERSION="$1"
MESSAGE="${2:-Release $VERSION}"

# Validate version format
if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+(\.[0-9]+)?(-[a-zA-Z0-9]+)?$ ]]; then
    print_error "Version must be in format v#.#.# or v#.#-suffix (e.g., v2.0.2, v2.0-alpha, v1.0.0-beta)"
    exit 1
fi

# Parse numeric components for the C header. We need MAJOR/MINOR/PATCH
# individually because they're three separate #define lines.
NUMERIC_VERSION=${VERSION#v}
NUMERIC_CORE=${NUMERIC_VERSION%%-*}   # strip any -alpha/-beta suffix
VMAJOR=$(echo "$NUMERIC_CORE" | cut -d. -f1)
VMINOR=$(echo "$NUMERIC_CORE" | cut -d. -f2)
VPATCH=$(echo "$NUMERIC_CORE" | cut -d. -f3)
VPATCH=${VPATCH:-0}

if (( DRY_RUN )); then
    print_warning "DRY-RUN MODE — no files will be modified, no git operations will run"
fi

print_status "Creating release $VERSION  (MAJOR=$VMAJOR MINOR=$VMINOR PATCH=$VPATCH)"

# Check repo
if [ ! -d ".git" ]; then
    print_error "This script must be run from the root of a git repository"
    exit 1
fi

# Uncommitted changes (in real mode, prompt; in dry-run, just warn)
if [ -n "$(git status --porcelain)" ]; then
    print_warning "You have uncommitted changes:"
    git status --porcelain
    if (( ! DRY_RUN )); then
        echo
        read -r -p "Do you want to continue? (y/N): " -n 1 REPLY
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            print_error "Release cancelled"
            exit 1
        fi
    fi
fi

# Tag uniqueness
if git tag -l | grep -q "^$VERSION$"; then
    print_error "Tag $VERSION already exists"
    exit 1
fi

CURRENT_BRANCH=$(git branch --show-current)
print_status "Current branch: $CURRENT_BRANCH"

# ---------- file edits (skipped in dry-run; previewed via diff) ----------
print_status "Updating version information..."

apply_sed() {
    # apply_sed <file> <sed-expr>
    local file="$1" expr="$2"
    if (( DRY_RUN )); then
        local tmp; tmp=$(mktemp)
        sed -E "$expr" "$file" > "$tmp"
        if ! diff -q "$file" "$tmp" >/dev/null 2>&1; then
            print_status "[dry-run] would change $file:"
            diff -u "$file" "$tmp" | sed 's/^/    /' | head -n 20
        else
            print_warning "[dry-run] $file: sed expression matched nothing"
        fi
        rm -f "$tmp"
    else
        sed -i.bak -E "$expr" "$file"
    fi
}

# CMakeLists.txt: bump project(... VERSION X.Y.Z ...) — replace whole VERSION token.
if [ -f "CMakeLists.txt" ] && grep -q "project.*VERSION" CMakeLists.txt; then
    apply_sed CMakeLists.txt "s/(project\\([^)]*VERSION )[0-9]+\\.[0-9]+\\.[0-9]+/\\1$NUMERIC_CORE/"
fi

# src/core/ubuilder.h: three separate #define lines (the OLD script's
# blanket s/#define UBUILDER_VERSION.*/.../ would clobber all three to
# the same string — that's the bug we're fixing here).
if [ -f "src/core/ubuilder.h" ]; then
    apply_sed src/core/ubuilder.h "s/^(#define UBUILDER_VERSION_MAJOR) [0-9]+/\\1 $VMAJOR/"
    apply_sed src/core/ubuilder.h "s/^(#define UBUILDER_VERSION_MINOR) [0-9]+/\\1 $VMINOR/"
    apply_sed src/core/ubuilder.h "s/^(#define UBUILDER_VERSION_PATCH) [0-9]+/\\1 $VPATCH/"
fi

# CHANGELOG: convert "[Unreleased]" → "[VERSION] - DATE" if present; that
# preserves the curated unreleased block. If [VERSION] is already there
# (operator pre-dated it manually), leave it alone — the step is idempotent.
# Only fall back to prepending a stub when neither header exists.
TODAY=$(date +%Y-%m-%d)
if [ -f "CHANGELOG.md" ]; then
    # Check both "[v2.1.0]" and "[2.1.0]" — Keep-a-Changelog drops the v.
    if grep -qE "^## \\[(${VERSION}|${NUMERIC_VERSION})\\]" CHANGELOG.md; then
        print_status "CHANGELOG.md already has an entry for this version — no edit needed"
    elif grep -qE "^## \\[Unreleased\\]" CHANGELOG.md; then
        apply_sed CHANGELOG.md "s/^## \\[Unreleased\\].*$/## [$VERSION] - $TODAY/"
    else
        print_warning "CHANGELOG.md has no [Unreleased] block — prepending a stub"
        if (( ! DRY_RUN )); then
            {
                echo "# Changelog"
                echo ""
                echo "## [$VERSION] - $TODAY"
                echo ""
                echo "### Added"
                echo "- $MESSAGE"
                echo ""
                tail -n +2 CHANGELOG.md
            } > CHANGELOG.md.tmp
            mv CHANGELOG.md.tmp CHANGELOG.md
        else
            print_status "[dry-run] would prepend stub entry for $VERSION"
        fi
    fi
fi

# ---------- git operations ----------
if (( DRY_RUN )); then
    print_status "[dry-run] would run: git add . ; git commit -m 'chore: bump version to $VERSION'"
    print_status "[dry-run] would run: git tag -a $VERSION -m \"$MESSAGE\""
    print_status "[dry-run] would run: git push origin $CURRENT_BRANCH $VERSION"
    print_success "Dry-run complete. Re-run without --dry-run to actually cut the release."
    exit 0
fi

print_status "Staging changes..."
git add .

if [ -n "$(git diff --cached --name-only)" ]; then
    print_status "Committing version updates..."
    git commit -m "chore: bump version to $VERSION"
else
    print_status "No version files to update"
fi

print_status "Creating tag $VERSION..."
git tag -a "$VERSION" -m "$MESSAGE"

print_status "Pushing changes and tag to origin..."
git push origin "$CURRENT_BRANCH"
git push origin "$VERSION"

print_success "Release $VERSION created successfully!"
print_success "GitHub Actions will now build and publish the release automatically."
print_success "Check the Actions tab on GitHub to monitor the release process."
ORIGIN_URL=$(git config remote.origin.url 2>/dev/null || true)
REPO_SLUG=$(echo "$ORIGIN_URL" | sed 's/.*github.com[:/]\(.*\)\.git/\1/')
if [ -n "$REPO_SLUG" ]; then
    print_success "Release will be available at: https://github.com/$REPO_SLUG/releases/tag/$VERSION"
fi

# Clean up backup files left by sed -i.bak
rm -f CMakeLists.txt.bak src/core/ubuilder.h.bak CHANGELOG.md.bak 2>/dev/null || true

echo
print_status "Next steps:"
echo "1. Monitor the GitHub Actions workflow"
echo "2. Once complete, visit the Releases page to verify the release"
echo "3. Test the published binaries if needed"
echo "4. Share the release with users!"
