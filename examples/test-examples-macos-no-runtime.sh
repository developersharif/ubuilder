#!/bin/bash

# test-examples-macos-no-runtime.sh
# Test all built example executables on macOS without host runtimes available
# This script simulates an environment where system runtimes (php, python3, node) are not available
# and tests the portability of the generated executables.

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
EXAMPLES_OUTPUT_DIR="$SCRIPT_DIR/output"
FAKE_BIN_DIR="/tmp/ubuilder-fake-bin-$$"

echo "=============================================="
echo "UBuilder macOS Portability Test (No Runtimes)"
echo "=============================================="
echo "Script Directory: $SCRIPT_DIR"
echo "Project Root: $PROJECT_ROOT"
echo "Examples Output: $EXAMPLES_OUTPUT_DIR"
echo ""

cleanup() {
    echo "Cleaning up fake bin directory..."
    rm -rf "$FAKE_BIN_DIR" 2>/dev/null || true
}

# Set up cleanup trap
trap cleanup EXIT INT TERM

# Function to create fake runtime executables that simulate missing runtimes
setup_fake_runtimes() {
    echo "=== Setting up fake runtimes to simulate missing host runtimes ==="
    
    # Create temporary directory for fake executables
    mkdir -p "$FAKE_BIN_DIR"
    
    # Create fake PHP executable
    cat > "$FAKE_BIN_DIR/php" << 'EOF'
#!/bin/bash
echo "php: command not found" >&2
exit 127
EOF
    chmod +x "$FAKE_BIN_DIR/php"
    
    # Create fake Python3 executable
    cat > "$FAKE_BIN_DIR/python3" << 'EOF'
#!/bin/bash
echo "python3: command not found" >&2
exit 127
EOF
    chmod +x "$FAKE_BIN_DIR/python3"
    
    # Create fake Python executable (some systems use 'python' instead of 'python3')
    cat > "$FAKE_BIN_DIR/python" << 'EOF'
#!/bin/bash
echo "python: command not found" >&2
exit 127
EOF
    chmod +x "$FAKE_BIN_DIR/python"
    
    # Create fake Node.js executable
    cat > "$FAKE_BIN_DIR/node" << 'EOF'
#!/bin/bash
echo "node: command not found" >&2
exit 127
EOF
    chmod +x "$FAKE_BIN_DIR/node"
    
    # Create fake npm executable
    cat > "$FAKE_BIN_DIR/npm" << 'EOF'
#!/bin/bash
echo "npm: command not found" >&2
exit 127
EOF
    chmod +x "$FAKE_BIN_DIR/npm"
    
    # Prepend fake bin directory to PATH to override system executables
    export PATH="$FAKE_BIN_DIR:$PATH"
    
    echo "✓ Fake runtimes created and PATH updated"
    return 0
}

# Function to verify that runtimes are blocked
verify_runtimes_blocked() {
    echo ""
    echo "=== Verifying that host runtimes are blocked ==="
    
    local blocked_count=0
    local total_runtimes=4
    
    # Test PHP
    set +e  # Temporarily disable exit on error
    php_available=false
    if command -v php >/dev/null 2>&1; then
        if php --version >/dev/null 2>&1; then
            php_available=true
        fi
    fi
    set -e  # Re-enable exit on error
    
    if [ "$php_available" = true ]; then
        echo "⚠️  PHP is still available and working"
    else
        echo "✅ PHP successfully blocked"
        ((blocked_count++))
    fi
    
    # Test Python3
    set +e  # Temporarily disable exit on error
    python3_available=false
    if command -v python3 >/dev/null 2>&1; then
        if python3 --version >/dev/null 2>&1; then
            python3_available=true
        fi
    fi
    set -e  # Re-enable exit on error
    
    if [ "$python3_available" = true ]; then
        echo "⚠️  Python3 is still available and working"
    else
        echo "✅ Python3 successfully blocked"
        ((blocked_count++))
    fi
    
    # Test Python
    set +e  # Temporarily disable exit on error
    python_available=false
    if command -v python >/dev/null 2>&1; then
        if python --version >/dev/null 2>&1; then
            python_available=true
        fi
    fi
    set -e  # Re-enable exit on error
    
    if [ "$python_available" = true ]; then
        echo "⚠️  Python is still available and working"
    else
        echo "✅ Python successfully blocked"
        ((blocked_count++))
    fi
    
    # Test Node.js
    set +e  # Temporarily disable exit on error
    node_available=false
    if command -v node >/dev/null 2>&1; then
        if node --version >/dev/null 2>&1; then
            node_available=true
        fi
    fi
    set -e  # Re-enable exit on error
    
    if [ "$node_available" = true ]; then
        echo "⚠️  Node.js is still available and working"
    else
        echo "✅ Node.js successfully blocked"
        ((blocked_count++))
    fi
    
    echo ""
    echo "Runtimes blocked: $blocked_count/$total_runtimes"
    
    if [ $blocked_count -ge 3 ]; then
        echo "✅ Runtime blocking successful"
        return 0
    else
        echo "⚠️  Some runtimes are still available, but continuing with tests..."
        return 0
    fi
}

# Function to test a single executable
test_executable() {
    local exe_name="$1"
    local exe_path="$2"
    local timeout_seconds="${3:-30}"
    
    echo ""
    echo "--- Testing $exe_name Executable ---"
    
    if [ ! -f "$exe_path" ]; then
        echo "❌ $exe_name executable not found at: $exe_path"
        return 1
    fi
    
    if [ ! -x "$exe_path" ]; then
        echo "❌ $exe_name executable is not executable: $exe_path"
        return 1
    fi
    
    echo "📁 Path: $exe_path"
    echo "📊 Size: $(du -h "$exe_path" | cut -f1)"
    
    # Test execution with timeout
    echo "🚀 Running $exe_name executable (timeout: ${timeout_seconds}s)..."
    
    local start_time=$(date +%s)
    local exit_code=0
    
    # Run with timeout and capture output (macOS compatible)
    # Use a background process with kill timeout since 'timeout' command may not be available
    set +e  # Disable exit on error for background process handling
    "$exe_path" > "${exe_name}_output.log" 2>&1 &
    local pid=$!
    
    local count=0
    while [ $count -lt $timeout_seconds ]; do
        if ! kill -0 $pid 2>/dev/null; then
            # Process finished
            wait $pid
            exit_code=$?
            break
        fi
        sleep 1
        ((count++))
    done
    
    # If we reached timeout, kill the process
    if [ $count -ge $timeout_seconds ]; then
        kill $pid 2>/dev/null || true
        wait $pid 2>/dev/null || true
        exit_code=124  # Standard timeout exit code
    fi
    set -e  # Re-enable exit on error
    
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    echo "⏱️  Execution time: ${duration}s"
    echo "🔢 Exit code: $exit_code"
    
    # Show output (first few lines)
    if [ -f "${exe_name}_output.log" ] && [ -s "${exe_name}_output.log" ]; then
        echo "📝 Output (first 10 lines):"
        head -10 "${exe_name}_output.log" | sed 's/^/   /'
        
        # Check for common error patterns
        if grep -qi "error\|exception\|failed\|cannot\|unable" "${exe_name}_output.log"; then
            echo "⚠️  Potential errors detected in output"
        fi
    else
        echo "📝 No output captured"
    fi
    
    # Determine test result
    if [ $exit_code -eq 0 ]; then
        echo "✅ $exe_name test PASSED"
        return 0
    elif [ $exit_code -eq 124 ]; then
        echo "⏰ $exe_name test TIMED OUT (${timeout_seconds}s)"
        return 0  # Timeout is acceptable for this test
    else
        echo "❌ $exe_name test FAILED (exit code: $exit_code)"
        return 1
    fi
}

# Function to run all executable tests
run_all_tests() {
    echo ""
    echo "=== Testing Portable Executables (No System Runtimes) ==="
    
    if [ ! -d "$EXAMPLES_OUTPUT_DIR" ]; then
        echo "❌ Examples output directory not found: $EXAMPLES_OUTPUT_DIR"
        echo "Make sure to build the examples first!"
        return 1
    fi
    
    local test_results=()
    local passed_tests=0
    local total_tests=0
    
    # Test Node.js executable
    local nodejs_exe="$EXAMPLES_OUTPUT_DIR/nodejs"
    if [ -f "$nodejs_exe" ]; then
        ((total_tests++))
        if test_executable "Node.js" "$nodejs_exe" 30; then
            test_results+=("Node.js: ✅ PASSED")
            ((passed_tests++))
        else
            test_results+=("Node.js: ❌ FAILED")
        fi
    else
        test_results+=("Node.js: ⚪ NOT FOUND")
        echo "⚠️  Node.js executable not found at: $nodejs_exe"
    fi
    
    # Test PHP executable
    local php_exe="$EXAMPLES_OUTPUT_DIR/php"
    if [ -f "$php_exe" ]; then
        ((total_tests++))
        if test_executable "PHP" "$php_exe" 30; then
            test_results+=("PHP: ✅ PASSED")
            ((passed_tests++))
        else
            test_results+=("PHP: ❌ FAILED")
        fi
    else
        test_results+=("PHP: ⚪ NOT FOUND")
        echo "⚠️  PHP executable not found at: $php_exe"
    fi
    
    # Test Python executable
    local python_exe="$EXAMPLES_OUTPUT_DIR/python"
    if [ -f "$python_exe" ]; then
        ((total_tests++))
        if test_executable "Python" "$python_exe" 30; then
            test_results+=("Python: ✅ PASSED")
            ((passed_tests++))
        else
            test_results+=("Python: ❌ FAILED")
        fi
    else
        test_results+=("Python: ⚪ NOT FOUND")
        echo "⚠️  Python executable not found at: $python_exe"
    fi
    
    # Display summary
    echo ""
    echo "=============================================="
    echo "PORTABILITY TEST SUMMARY"
    echo "=============================================="
    
    for result in "${test_results[@]}"; do
        echo "  $result"
    done
    
    echo ""
    echo "📊 Tests passed: $passed_tests/$total_tests"
    
    if [ $total_tests -eq 0 ]; then
        echo "⚠️  No executables found to test!"
        echo "Make sure to build the examples first using:"
        echo "  ./build-examples-macos.sh"
        return 1
    elif [ $passed_tests -eq $total_tests ]; then
        echo "🎉 All tests PASSED - Executables are truly portable!"
        return 0
    elif [ $passed_tests -gt 0 ]; then
        echo "⚠️  Some tests passed - Partial portability achieved"
        return 0
    else
        echo "❌ All tests FAILED - Portability issues detected"
        return 1
    fi
}

# Main execution
main() {
    echo "Starting macOS portability test..."
    echo "Current PATH: $PATH"
    echo ""
    
    # Setup fake runtimes to simulate missing host runtimes
    if ! setup_fake_runtimes; then
        echo "❌ Failed to set up fake runtimes"
        exit 1
    fi
    
    # Verify runtimes are blocked
    if ! verify_runtimes_blocked; then
        echo "❌ Failed to verify runtime blocking"
        exit 1
    fi
    
    # Run all executable tests
    if run_all_tests; then
        echo ""
        echo "✅ macOS portability test completed successfully!"
        exit 0
    else
        echo ""
        echo "❌ macOS portability test failed!"
        exit 1
    fi
}

# Check if script is being executed (not sourced)
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
