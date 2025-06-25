#!/bin/bash

# UBuilder Example Build Script
# Demonstrates building all example applications

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
UBUILDER="${PROJECT_ROOT}/dist/bin/ubuilder"
EXAMPLES_DIR="${PROJECT_ROOT}/examples"
OUTPUT_DIR="${PROJECT_ROOT}/example-builds"

echo "UBuilder Example Build Script"
echo "============================="
echo

# Check if UBuilder is built
if [ ! -f "${UBUILDER}" ]; then
    echo "Error: UBuilder not found at ${UBUILDER}"
    echo "Please run build.sh first to build UBuilder."
    exit 1
fi

# Create output directory
mkdir -p "${OUTPUT_DIR}"

# Build Python example
echo "Building Python Hello World example..."
"${UBUILDER}" --project-dir="${EXAMPLES_DIR}/python-hello" \
              --runtime=python \
              --output="${OUTPUT_DIR}/python-hello" \
              --entry-point=main.py \
              --verbose

# Build PHP example
echo "Building PHP Hello World example..."
"${UBUILDER}" --project-dir="${EXAMPLES_DIR}/php-hello" \
              --runtime=php \
              --output="${OUTPUT_DIR}/php-hello" \
              --entry-point=main.php \
              --verbose

# Build Node.js example
echo "Building Node.js Hello World example..."
"${UBUILDER}" --project-dir="${EXAMPLES_DIR}/node-hello" \
              --runtime=node \
              --output="${OUTPUT_DIR}/node-hello" \
              --entry-point=main.js \
              --verbose

echo
echo "Example builds completed!"
echo "Built applications:"
echo "  ${OUTPUT_DIR}/python-hello"
echo "  ${OUTPUT_DIR}/php-hello"
echo "  ${OUTPUT_DIR}/node-hello"
echo
echo "Test the applications:"
echo "  ${OUTPUT_DIR}/python-hello"
echo "  ${OUTPUT_DIR}/php-hello"
echo "  ${OUTPUT_DIR}/node-hello"
