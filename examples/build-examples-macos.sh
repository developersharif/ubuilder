#!/bin/bash

# UBuilder Examples Build and Test Script for macOS
# Builds UBuilder core, creates executables from all examples, and runs them

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
BUILD_DIR="${PROJECT_ROOT}/build"
EXAMPLES_DIR="${PROJECT_ROOT}/examples"
OUTPUT_DIR="${EXAMPLES_DIR}/output"

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_header() {
    echo -e "${PURPLE}===========================================${NC}"
    echo -e "${PURPLE} $1${NC}"
    echo -e "${PURPLE}===========================================${NC}"
}

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to check runtime availability (macOS-specific paths)
check_runtime() {
    local runtime=$1
    case $runtime in
        "python")
            if command_exists python3; then
                log_success "Python3 runtime available: $(python3 --version)"
                return 0
            elif command_exists python; then
                log_success "Python runtime available: $(python --version)"
                return 0
            else
                log_warning "Python runtime not available"
                return 1
            fi
            ;;
        "php")
            if command_exists php; then
                log_success "PHP runtime available: $(php --version | head -n1)"
                return 0
            else
                log_warning "PHP runtime not available - try: brew install php"
                return 1
            fi
            ;;
        "node")
            if command_exists node; then
                log_success "Node.js runtime available: $(node --version)"
                return 0
            else
                log_warning "Node.js runtime not available - try: brew install node"
                return 1
            fi
            ;;
        *)
            log_error "Unknown runtime: $runtime"
            return 1
            ;;
    esac
}

# Function to build UBuilder core (macOS-specific)
build_ubuilder() {
    log_header "Building UBuilder Core (macOS)"
    
    cd "${PROJECT_ROOT}"
    
    if [ -d "${BUILD_DIR}" ]; then
        log_info "Cleaning previous build..."
        rm -rf "${BUILD_DIR}"
    fi
    
    log_info "Creating build directory..."
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    
    log_info "Configuring with CMake..."
    if ! cmake -DCMAKE_BUILD_TYPE=Release \
               -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
               "${PROJECT_ROOT}"; then
        log_error "CMake configuration failed"
        return 1
    fi
    
    log_info "Building UBuilder..."
    local cpu_count=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    if ! make -j${cpu_count}; then
        log_error "Build failed"
        return 1
    fi
    
    if [ ! -f "${BUILD_DIR}/src/ubuilder" ]; then
        log_error "UBuilder executable not found after build"
        return 1
    fi
    
    log_success "UBuilder core built successfully"
    return 0
}

# Function to build example project (macOS-specific)
build_example() {
    local example_dir=$1
    local example_name=$(basename "$example_dir")
    local runtime=""
    local output_executable="${OUTPUT_DIR}/${example_name}"
    
    log_info "Building example: $example_name"
    
    # Read runtime from ubuilder.json
    if [ -f "${example_dir}/ubuilder.json" ]; then
        runtime=$(grep -o '"runtime"[[:space:]]*:[[:space:]]*"[^"]*"' "${example_dir}/ubuilder.json" | cut -d'"' -f4)
    else
        log_error "No ubuilder.json found in $example_dir"
        return 1
    fi
    
    # Check if runtime is available
    if ! check_runtime "$runtime"; then
        log_warning "Skipping $example_name due to missing $runtime runtime"
        return 2  # Special return code for missing runtime
    fi

    # PHP on macOS works when host PHP is statically linked (Laravel Herd,
    # static-php-cli output): the builder detects a non-existent
    # extension_dir and ships the binary as-is, since every extension is
    # already baked into the executable. Dynamic Homebrew PHP on macOS
    # still hits the libxml2/dyld portability caveat documented for Linux.
    # Build the example
    log_info "Creating executable for $example_name (runtime: $runtime)..."
    if ! "${BUILD_DIR}/src/ubuilder" --project-dir="$example_dir" --runtime="$runtime" --output="$output_executable"; then
        log_error "Failed to build $example_name"
        return 1
    fi
    
    if [ ! -f "$output_executable" ]; then
        log_error "Executable not created: $output_executable"
        return 1
    fi
    
    # Make executable
    chmod +x "$output_executable"
    
    log_success "Built $example_name -> $output_executable"
    return 0
}

# Function to run example (macOS-specific)
run_example() {
    local output_executable=$1
    local example_name=$(basename "$output_executable")
    
    log_info "Running $example_name..."
    
    if [ ! -f "$output_executable" ]; then
        log_error "Executable not found: $output_executable"
        return 1
    fi
    
    if ! [ -x "$output_executable" ]; then
        log_error "File is not executable: $output_executable"
        return 1
    fi
    
    echo -e "${CYAN}--- Output from $example_name ---${NC}"
    if ! "$output_executable"; then
        log_error "Failed to run $example_name"
        echo -e "${CYAN}--- End output ---${NC}"
        return 1
    fi
    echo -e "${CYAN}--- End output ---${NC}"
    
    log_success "Successfully ran $example_name"
    return 0
}

# Main execution
main() {
    log_header "UBuilder Examples Build and Test (macOS)"
    
    # Check if we're on macOS
    if [[ "$OSTYPE" != "darwin"* ]]; then
        log_error "This script is designed for macOS. Use build-examples.sh for other platforms."
        exit 1
    fi
    
    # Check for Homebrew (recommended for macOS)
    if ! command_exists brew; then
        log_warning "Homebrew not found. Some runtimes might not be available."
        log_info "Install Homebrew: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    fi
    
    # Create output directory
    mkdir -p "${OUTPUT_DIR}"
    
    # Step 1: Build UBuilder core
    if ! build_ubuilder; then
        log_error "Failed to build UBuilder core. Aborting."
        exit 1
    fi
    
    # Step 2: Find and build all examples
    log_header "Building All Examples"
    
    local examples=()
    local built_examples=()
    local skipped_examples=()
    local failed_examples=()
    
    # Find all example directories (those containing ubuilder.json)
    for example_dir in "${EXAMPLES_DIR}"/*; do
        if [ -d "$example_dir" ] && [ -f "$example_dir/ubuilder.json" ]; then
            examples+=("$example_dir")
        fi
    done
    
    if [ ${#examples[@]} -eq 0 ]; then
        log_warning "No example projects found"
        exit 0
    fi
    
    log_info "Found ${#examples[@]} example projects"
    
    # Build each example
    for example_dir in "${examples[@]}"; do
        local example_name=$(basename "$example_dir")
        if build_example "$example_dir"; then
            built_examples+=("${OUTPUT_DIR}/${example_name}")
        else
            local exit_code=$?
            if [ $exit_code -eq 2 ]; then
                skipped_examples+=("$example_name")
            else
                failed_examples+=("$example_name")
            fi
        fi
    done
    
    # Step 3: Run all successfully built examples
    if [ ${#built_examples[@]} -gt 0 ]; then
        log_header "Running All Built Examples"
        
        local run_successful=()
        local run_failed=()
        
        for executable in "${built_examples[@]}"; do
            if run_example "$executable"; then
                run_successful+=("$(basename "$executable")")
            else
                run_failed+=("$(basename "$executable")")
            fi
        done
        
        # Final summary
        log_header "Build and Test Summary (macOS)"
        
        echo -e "${BLUE}Total examples found:${NC} ${#examples[@]}"
        echo -e "${GREEN}Successfully built:${NC} ${#built_examples[@]}"
        echo -e "${YELLOW}Skipped (missing runtime):${NC} ${#skipped_examples[@]}"
        echo -e "${RED}Build failures:${NC} ${#failed_examples[@]}"
        echo -e "${GREEN}Successfully ran:${NC} ${#run_successful[@]}"
        echo -e "${RED}Runtime failures:${NC} ${#run_failed[@]}"
        
        if [ ${#built_examples[@]} -gt 0 ]; then
            echo -e "\n${GREEN}Built executables:${NC}"
            for executable in "${built_examples[@]}"; do
                echo -e "  • $executable"
            done
        fi
        
        if [ ${#skipped_examples[@]} -gt 0 ]; then
            echo -e "\n${YELLOW}Skipped examples (missing runtime):${NC}"
            for example in "${skipped_examples[@]}"; do
                echo -e "  • $example"
            done
            echo -e "\n${YELLOW}Install missing runtimes with:${NC}"
            echo -e "  brew install python php node"
        fi
        
        if [ ${#failed_examples[@]} -gt 0 ]; then
            echo -e "\n${RED}Failed builds:${NC}"
            for example in "${failed_examples[@]}"; do
                echo -e "  • $example"
            done
        fi
        
        if [ ${#run_failed[@]} -gt 0 ]; then
            echo -e "\n${RED}Runtime failures:${NC}"
            for example in "${run_failed[@]}"; do
                echo -e "  • $example"
            done
        fi
        
        # Overall success check
        if [ ${#run_successful[@]} -eq ${#built_examples[@]} ] && [ ${#built_examples[@]} -gt 0 ]; then
            echo -e "\n${GREEN}🎉 ALL BUILDS AND TESTS SUCCESSFUL ON macOS! 🎉${NC}"
            echo -e "${GREEN}All ${#built_examples[@]} example(s) built and ran successfully!${NC}"
            exit 0
        else
            if [ ${#built_examples[@]} -eq 0 ]; then
                echo -e "\n${RED}❌ No examples could be built${NC}"
            else
                echo -e "\n${YELLOW}⚠️  Some examples failed to run properly${NC}"
            fi
            exit 1
        fi
    else
        log_error "No examples were successfully built"
        exit 1
    fi
}

# Check if we're being sourced or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
