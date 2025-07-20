#!/bin/bash
# Run macOS 'leaks' tool on all Orus test files in all test directories
# Tests both debug and release builds for comprehensive memory leak detection

set -e

# Test both debug and release binaries
ORUS_BINARIES=("./orus_debug" "./orus")

# List of test directories
TEST_DIRS=(
    "tests/benchmarks"
    "tests/control_flow"
    "tests/edge_cases"
    "tests/expressions"
    "tests/literals"
    "tests/loops"
    "tests/register_file"
    "tests/strings"
    "tests/type_safety_fails"
    "tests/types"
    "tests/variables"
)

# Function to run leaks on a specific binary and test file
run_leaks_test() {
    local binary="$1"
    local test_file="$2"
    
    echo "Running leaks on $test_file with $binary..."
    
    # Run leaks and capture the output
    if leaks -atExit -- "$binary" "$test_file" 2>&1 | grep -q "Process.*leaks for.*total leaked bytes"; then
        echo "  ❌ MEMORY LEAK DETECTED in $test_file with $binary"
        return 1
    else
        echo "  ✅ No leaks detected in $test_file with $binary"
        return 0
    fi
}

# Ensure both binaries exist
for binary in "${ORUS_BINARIES[@]}"; do
    if [ ! -f "$binary" ]; then
        echo "Error: Binary $binary not found. Please run 'make' and 'make release' first."
        exit 1
    fi
done

echo "🔍 Starting comprehensive memory leak testing..."
echo "Testing binaries: ${ORUS_BINARIES[*]}"
echo "=========================================="

total_tests=0
failed_tests=0

# Test each binary with all test files
for binary in "${ORUS_BINARIES[@]}"; do
    echo ""
    echo "📋 Testing with binary: $binary"
    echo "----------------------------------------"
    
    for DIR in "${TEST_DIRS[@]}"; do
        if [ -d "$DIR" ]; then
            echo ""
            echo "📂 Testing directory: $DIR"
            
            for f in "$DIR"/*.orus; do
                if [ -f "$f" ]; then
                    total_tests=$((total_tests + 1))
                    
                    if ! run_leaks_test "$binary" "$f"; then
                        failed_tests=$((failed_tests + 1))
                    fi
                fi
            done
        fi
    done
done

echo ""
echo "=========================================="
echo "🎯 Memory Leak Testing Summary"
echo "=========================================="
echo "Total tests run: $total_tests"
echo "Failed tests: $failed_tests"
echo "Passed tests: $((total_tests - failed_tests))"

if [ $failed_tests -eq 0 ]; then
    echo "🎉 All tests passed! No memory leaks detected."
    exit 0
else
    echo "⚠️  $failed_tests test(s) failed with memory leaks."
    exit 1
fi
