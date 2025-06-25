#!/bin/bash

# UBuilder Build Script
# Builds UBuilder for the current platform

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INSTALL_DIR="${PROJECT_ROOT}/dist"

echo "UBuilder Build Script"
echo "===================="
echo "Project root: ${PROJECT_ROOT}"
echo "Build directory: ${BUILD_DIR}"
echo "Install directory: ${INSTALL_DIR}"
echo

# Clean previous build
if [ -d "${BUILD_DIR}" ]; then
    echo "Cleaning previous build..."
    rm -rf "${BUILD_DIR}"
fi

# Create build directory
echo "Creating build directory..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure with CMake
echo "Configuring with CMake..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
      "${PROJECT_ROOT}"

# Build
echo "Building UBuilder..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Install
echo "Installing UBuilder..."
make install

echo
echo "Build completed successfully!"
echo "UBuilder executable: ${INSTALL_DIR}/bin/ubuilder"
echo
echo "To test the build:"
echo "  ${INSTALL_DIR}/bin/ubuilder --version"
echo "  ${INSTALL_DIR}/bin/ubuilder --help"
