#!/bin/bash

# UBuilder Examples Build and Test Script
# Universal cross-platform script that detects OS and runs appropriate build script

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Project paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

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

# Main platform detection and delegation
PLATFORM=$(detect_platform)

echo -e "${BLUE}🌍 UBuilder Universal Build Script${NC}"
echo -e "${BLUE}Detected platform: ${PLATFORM}${NC}"
echo

case $PLATFORM in
    "linux")
        echo -e "${GREEN}Running Linux build script...${NC}"
        exec "${SCRIPT_DIR}/build-examples-linux.sh" "$@"
        ;;
    "macos")
        echo -e "${GREEN}Running macOS build script...${NC}"
        if [ -f "${SCRIPT_DIR}/build-examples-macos.sh" ]; then
            exec "${SCRIPT_DIR}/build-examples-macos.sh" "$@"
        else
            echo -e "${YELLOW}macOS-specific script not found, falling back to Linux script...${NC}"
            exec "${SCRIPT_DIR}/build-examples-linux.sh" "$@"
        fi
        ;;
    "windows")
        echo -e "${GREEN}Windows detected. Please use one of these scripts:${NC}"
        echo -e "${CYAN}  • build-examples.bat    (Batch script)${NC}"
        echo -e "${CYAN}  • build-examples.ps1    (PowerShell script)${NC}"
        echo
        echo -e "${YELLOW}Or use WSL/MSYS2/Git Bash to run this script.${NC}"
        exit 1
        ;;
    *)
        echo -e "${RED}Unknown platform: $OSTYPE${NC}"
        echo -e "${YELLOW}Attempting to run default Linux script...${NC}"
        exec "${SCRIPT_DIR}/build-examples-linux.sh" "$@"
        ;;
esac