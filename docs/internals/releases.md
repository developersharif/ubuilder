# UBuilder Release System

This document explains how to create and publish UBuilder releases.

## Overview

UBuilder uses GitHub Actions to automatically build and publish release binaries when you create a version tag. The system supports:

- **Linux (x86_64)**: Ubuntu 20.04+ compatible binaries
- **Windows (x86_64)**: Windows 10+ compatible binaries  
- **macOS (x86_64)**: macOS 12+ compatible binaries

## Creating a Release

### Method 1: Using the Release Script (Recommended)

#### On Linux/macOS:
```bash
# Create a new release
./create-release.sh v2.0.2 "Bug fixes and improvements"

# The script will:
# - Update version numbers in source files
# - Update CHANGELOG.md
# - Create and push the git tag
# - Trigger the GitHub Actions release workflow
```

#### On Windows:
```powershell
# Create a new release
.\create-release.ps1 -Version v2.0.2 -Message "Bug fixes and improvements"
```

### Method 2: Manual Tag Creation

```bash
# Update version information manually (optional)
# Edit CMakeLists.txt, src/core/ubuilder.h, CHANGELOG.md

# Create and push the tag
git tag -a v2.0.2 -m "Release v2.0.2 - Bug fixes and improvements"
git push origin v2.0.2
```

## Release Workflow

Once you create a version tag (format: `v#.#.#`), the GitHub Actions release workflow automatically:

1. **Builds binaries** for all supported platforms
2. **Runs tests** to ensure quality
3. **Creates packages** with binaries, examples, and documentation
4. **Publishes a GitHub Release** with downloadable archives
5. **Generates release notes** with download instructions

## Release Artifacts

Each release includes:

### Linux Package (`ubuilder-linux-amd64.tar.gz`)
- `ubuilder` - Main executable
- `examples/` - Pre-built example applications
- `README.md`, `LICENSE`, `CHANGELOG.md`

### Windows Package (`ubuilder-windows-amd64.zip`)
- `ubuilder.exe` - Main executable
- `examples/` - Pre-built example applications (.exe files)
- `README.md`, `LICENSE`, `CHANGELOG.md`

### macOS Package (`ubuilder-macos-amd64.tar.gz`)
- `ubuilder` - Main executable
- `examples/` - Pre-built example applications
- `README.md`, `LICENSE`, `CHANGELOG.md`

## Version Numbering

UBuilder follows [Semantic Versioning](https://semver.org/):

- **Major** (v2.0.0): Breaking changes
- **Minor** (v2.1.0): New features, backwards compatible
- **Patch** (v2.1.1): Bug fixes, backwards compatible

## Testing Releases

### Local Testing Before Release
```bash
# Build and test locally
./build-all.sh

# Test examples
./examples/build-examples.sh  # Linux/macOS
examples\build-examples.ps1   # Windows
```

### Testing Published Releases
1. Download the archive for your platform
2. Extract and test the binary:
   ```bash
   # Linux/macOS
   ./ubuilder --version
   ./ubuilder --help
   
   # Windows
   ubuilder.exe --version
   ubuilder.exe --help
   ```
3. Test with example projects
4. Verify examples run correctly

## Monitoring Releases

1. **GitHub Actions**: Monitor the workflow at `https://github.com/your-repo/actions`
2. **Release Page**: Check published releases at `https://github.com/your-repo/releases`
3. **Download Stats**: View download metrics on the release page

## Troubleshooting

### Release Build Failures
- Check the GitHub Actions logs for detailed error messages
- Verify all platforms build successfully in CI before tagging
- Ensure dependencies are properly installed on all platforms

### Missing Artifacts
- Check that all platforms completed successfully
- Verify the artifact upload didn't timeout
- Look for missing files in the build output

### Version Conflicts
- Ensure the tag name follows the `v#.#.#` format
- Check that the tag doesn't already exist
- Verify version numbers are updated in source files

## Manual Release Trigger

You can also trigger a release manually from GitHub:

1. Go to **Actions** → **Release UBuilder**
2. Click **Run workflow**
3. Enter the version tag (e.g., `v2.0.2`)
4. Click **Run workflow**

This is useful for:
- Re-running failed releases
- Creating releases from specific commits
- Testing the release process

## Post-Release Tasks

After a successful release:

1. **Test the published binaries** on each platform
2. **Update documentation** if needed
3. **Announce the release** to users
4. **Monitor for issues** and prepare patches if needed

## Example Release Process

```bash
# 1. Ensure you're on the main branch with latest changes
git checkout main
git pull origin main

# 2. Run local tests
./build-all.sh

# 3. Create the release
./create-release.sh v2.1.0 "Added new runtime support and improved performance"

# 4. Monitor GitHub Actions
# Visit: https://github.com/your-repo/actions

# 5. Verify the release
# Visit: https://github.com/your-repo/releases

# 6. Test published binaries
# Download and test each platform's archive
```

## Best Practices

- **Test thoroughly** before releasing
- **Use descriptive release messages**
- **Follow semantic versioning**
- **Update CHANGELOG.md** for each release
- **Monitor release workflows** for failures
- **Keep release notes clear** and user-friendly
