#!/bin/bash

# UBuilder Build All Script
# Universal cross-platform wrapper script to build and test all UBuilder projects

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Detect platform
detect_platform() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "linux"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ "$OSTYPE" == "cygwin" ]] || [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
        echo "windows"
    else
        echo "unknown"
    fi
}

PLATFORM=$(detect_platform)

echo -e "${BLUE}🚀 UBuilder Build All Projects${NC}"
echo -e "${BLUE}===============================${NC}"
echo -e "${BLUE}Platform: ${PLATFORM}${NC}"
echo

case $PLATFORM in
    "linux"|"macos"|"unknown")
        # Run the examples build script which handles everything
        if "${SCRIPT_DIR}/examples/build-examples.sh"; then
            echo
            echo -e "${GREEN}✅ All UBuilder projects built and tested successfully!${NC}"
            echo -e "${GREEN}   Check the examples/output/ directory for generated executables.${NC}"
            exit 0
        else
            echo
            echo -e "${RED}❌ Some builds or tests failed. Check the output above for details.${NC}"
            exit 1
        fi
        ;;
    "windows")
        echo -e "${YELLOW}Windows detected. Please use one of these commands:${NC}"
        echo
        echo -e "${BLUE}For Command Prompt:${NC}"
        echo -e "${CYAN}  examples\\build-examples.bat${NC}"
        echo
        echo -e "${BLUE}For PowerShell:${NC}"
        echo -e "${CYAN}  examples\\build-examples.ps1${NC}"
        echo
        echo -e "${BLUE}For WSL/MSYS2/Git Bash:${NC}"
        echo -e "${CYAN}  ./examples/build-examples.sh${NC}"
        echo
        exit 1
        ;;
esac
