#!/bin/bash

# Control Flow Test Suite Runner for Orus Language
# Tests if/elif/else statements and scoping behavior

echo "=== Orus Control Flow Test Suite ==="
echo "Testing if/elif/else statements and scoping..."
echo

# Build the interpreter if needed
if [ ! -f "../../orus_debug" ]; then
    echo "Building Orus interpreter..."
    cd ../..
    make debug > /dev/null 2>&1
    cd tests/control_flow
    echo "Build complete."
    echo
fi

INTERPRETER="../../orus_debug"
PASSED=0
FAILED=0
TOTAL=0

# Function to run a test
run_test() {
    local test_file="$1"
    local test_name="$2"
    
    echo "Running: $test_name"
    echo "File: $test_file"
    echo "---"
    
    if $INTERPRETER "$test_file" 2>/dev/null; then
        echo "✅ PASSED: $test_name"
        ((PASSED++))
    else
        echo "❌ FAILED: $test_name"
        echo "Error output:"
        $INTERPRETER "$test_file" 2>&1 | head -5
        ((FAILED++))
    fi
    
    ((TOTAL++))
    echo
}

# Run all control flow tests
echo "--- Basic If Statement Tests ---"
run_test "if_basic.orus" "Basic If Statements"

echo "--- If-Else Statement Tests ---" 
run_test "if_else_basic.orus" "Basic If-Else Statements"

echo "--- Elif Chain Tests ---"
run_test "elif_chains.orus" "Elif Chains and Multiple Conditions"

echo "--- Scoping Tests ---"
run_test "scoping_basic.orus" "Basic Block Scoping"
run_test "scoping_nested.orus" "Nested Block Scoping"

echo "--- Complex Condition Tests ---"
run_test "complex_conditions.orus" "Complex Conditional Expressions"

echo "--- Edge Case Tests ---"
run_test "edge_cases.orus" "Edge Cases and Boundary Conditions"

# Test summary
echo "=== Control Flow Test Summary ==="
echo "Total Tests: $TOTAL"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo

if [ $FAILED -eq 0 ]; then
    echo "🎉 ALL CONTROL FLOW TESTS PASSED! 🎉"
    echo "if/elif/else implementation is working perfectly!"
    exit 0
else
    echo "⚠️  Some tests failed. Check the output above."
    exit 1
fi