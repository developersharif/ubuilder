#!/bin/bash

# UBuilder Cross-Platform Build Script (Phase 2)
# Builds UBuilder for multiple platforms

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INSTALL_DIR="${PROJECT_ROOT}/dist"

echo "UBuilder Cross-Platform Build Script (Phase 2)"
echo "=============================================="
echo "Project root: ${PROJECT_ROOT}"
echo

# Detect current platform
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    CURRENT_PLATFORM="linux"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    CURRENT_PLATFORM="macos"
elif [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "msys" ]]; then
    CURRENT_PLATFORM="windows"
else
    CURRENT_PLATFORM="unknown"
fi

echo "Current platform: ${CURRENT_PLATFORM}"

# Function to build for a specific platform
build_platform() {
    local platform=$1
    local build_dir="${BUILD_DIR}/${platform}"
    local install_dir="${INSTALL_DIR}/${platform}"
    
    echo "Building for platform: ${platform}"
    echo "Build directory: ${build_dir}"
    echo "Install directory: ${install_dir}"
    
    # Clean and create build directory
    rm -rf "${build_dir}"
    mkdir -p "${build_dir}"
    cd "${build_dir}"
    
    # Configure CMake
    case "${platform}" in
        "linux")
            cmake -DCMAKE_BUILD_TYPE=Release \
                  -DCMAKE_INSTALL_PREFIX="${install_dir}" \
                  "${PROJECT_ROOT}"
            ;;
        "windows")
            # Cross-compilation for Windows (requires mingw)
            if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
                cmake -DCMAKE_BUILD_TYPE=Release \
                      -DCMAKE_INSTALL_PREFIX="${install_dir}" \
                      -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/mingw-toolchain.cmake" \
                      "${PROJECT_ROOT}"
            else
                echo "Warning: MinGW not found, skipping Windows build"
                return 1
            fi
            ;;
        "macos")
            # Cross-compilation for macOS (if on macOS)
            if [[ "$OSTYPE" == "darwin"* ]]; then
                cmake -DCMAKE_BUILD_TYPE=Release \
                      -DCMAKE_INSTALL_PREFIX="${install_dir}" \
                      "${PROJECT_ROOT}"
            else
                echo "Warning: macOS builds require macOS host, skipping"
                return 1
            fi
            ;;
    esac
    
    # Build
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    
    # Install
    make install
    
    echo "Build completed for ${platform}"
    echo
}

# Build for current platform
build_platform "${CURRENT_PLATFORM}"

# Build for other platforms if cross-compilation tools are available
if [[ "${CURRENT_PLATFORM}" == "linux" ]]; then
    echo "Checking for cross-compilation capabilities..."
    
    # Try to build for Windows if MinGW is available
    if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
        echo "MinGW found, building for Windows..."
        build_platform "windows" || echo "Windows build failed"
    fi
fi

echo "Multi-platform build completed!"
echo "Built executables:"
find "${INSTALL_DIR}" -name "ubuilder*" -type f 2>/dev/null || echo "No executables found"
