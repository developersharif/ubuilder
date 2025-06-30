#!/bin/bash
# UBuilder Release Creation Script
# Usage: ./create-release.sh <version> [message]
# Example: ./create-release.sh v2.0.2 "Bug fixes and improvements"

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check parameters
if [ $# -lt 1 ]; then
    print_error "Usage: $0 <version> [message]"
    print_error "Example: $0 v2.0.2 \"Bug fixes and improvements\""
    exit 1
fi

VERSION="$1"
MESSAGE="${2:-Release $VERSION}"

# Validate version format
if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    print_error "Version must be in format v#.#.# (e.g., v2.0.2)"
    exit 1
fi

print_status "Creating release $VERSION"

# Check if we're in a git repository
if [ ! -d ".git" ]; then
    print_error "This script must be run from the root of a git repository"
    exit 1
fi

# Check for uncommitted changes
if [ -n "$(git status --porcelain)" ]; then
    print_warning "You have uncommitted changes:"
    git status --porcelain
    echo
    read -p "Do you want to continue? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        print_error "Release cancelled"
        exit 1
    fi
fi

# Check if tag already exists
if git tag -l | grep -q "^$VERSION$"; then
    print_error "Tag $VERSION already exists"
    exit 1
fi

# Get current branch
CURRENT_BRANCH=$(git branch --show-current)
print_status "Current branch: $CURRENT_BRANCH"

# Update version in source files if needed
print_status "Updating version information..."

# Update version in CMakeLists.txt if it exists
if [ -f "CMakeLists.txt" ]; then
    # Extract numeric version (remove 'v' prefix)
    NUMERIC_VERSION=${VERSION#v}
    
    # Update project version in CMakeLists.txt
    if grep -q "project.*VERSION" CMakeLists.txt; then
        sed -i.bak "s/project([^)]*VERSION [^)]*)/project(UBuilder VERSION $NUMERIC_VERSION)/" CMakeLists.txt
        print_status "Updated version in CMakeLists.txt"
    fi
fi

# Update version in source code header if it exists
if [ -f "src/core/ubuilder.h" ]; then
    if grep -q "#define UBUILDER_VERSION" src/core/ubuilder.h; then
        sed -i.bak "s/#define UBUILDER_VERSION.*/#define UBUILDER_VERSION \"$VERSION\"/" src/core/ubuilder.h
        print_status "Updated version in ubuilder.h"
    fi
fi

# Update CHANGELOG.md
print_status "Updating CHANGELOG.md..."
if [ -f "CHANGELOG.md" ]; then
    # Create a temporary file with the new entry
    {
        echo "# Changelog"
        echo ""
        echo "## [$VERSION] - $(date +%Y-%m-%d)"
        echo ""
        echo "### Added"
        echo "- $MESSAGE"
        echo ""
        # Add the rest of the existing changelog (skip the first line if it's "# Changelog")
        tail -n +2 CHANGELOG.md
    } > CHANGELOG.md.tmp
    
    mv CHANGELOG.md.tmp CHANGELOG.md
    print_status "Updated CHANGELOG.md"
else
    # Create new CHANGELOG.md
    cat > CHANGELOG.md << EOF
# Changelog

## [$VERSION] - $(date +%Y-%m-%d)

### Added
- $MESSAGE

EOF
    print_status "Created CHANGELOG.md"
fi

# Add changes to git
print_status "Staging changes..."
git add .

# Check if there are changes to commit
if [ -n "$(git diff --cached --name-only)" ]; then
    print_status "Committing version updates..."
    git commit -m "chore: bump version to $VERSION"
else
    print_status "No version files to update"
fi

# Create and push tag
print_status "Creating tag $VERSION..."
git tag -a "$VERSION" -m "$MESSAGE"

print_status "Pushing changes and tag to origin..."
git push origin "$CURRENT_BRANCH"
git push origin "$VERSION"

print_success "Release $VERSION created successfully!"
print_success "GitHub Actions will now build and publish the release automatically."
print_success "Check the Actions tab on GitHub to monitor the release process."
print_success "Release will be available at: https://github.com/$(git config remote.origin.url | sed 's/.*github.com[:/]\(.*\)\.git/\1/')/releases/tag/$VERSION"

# Clean up backup files
rm -f CMakeLists.txt.bak src/core/ubuilder.h.bak 2>/dev/null || true

echo
print_status "Next steps:"
echo "1. Monitor the GitHub Actions workflow"
echo "2. Once complete, visit the Releases page to verify the release"
echo "3. Test the published binaries if needed"
echo "4. Share the release with users!"
