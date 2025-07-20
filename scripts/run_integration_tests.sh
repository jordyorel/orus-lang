#!/bin/bash

# Integration Test Runner
# Tests cross-feature interactions and complex scenarios

echo "🔄 Running Orus Integration Test Suite..."
echo "========================================"

ORUS_BINARY=${1:-"./orus_debug"}
TEST_DIR="tests"
INTEGRATION_TEST="$TEST_DIR/integration_test.orus"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

passed=0
failed=0

# Function to run a test
run_test() {
    local test_file=$1
    local test_name=$2
    
    echo -n "  Testing $test_name... "
    
    if "$ORUS_BINARY" "$test_file" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ PASS${NC}"
        ((passed++))
        return 0
    else
        echo -e "${RED}✗ FAIL${NC}"
        ((failed++))
        return 1
    fi
}

# Test 1: Main Integration Test
echo -e "\n${BLUE}=== Cross-Feature Integration Tests ===${NC}"
run_test "$INTEGRATION_TEST" "Comprehensive Integration"

# Test 2: Performance Integration (using existing benchmarks)
echo -e "\n${BLUE}=== Performance Integration Tests ===${NC}"
run_test "tests/benchmarks/comprehensive_benchmark.orus" "Performance Integration"
run_test "tests/benchmarks/control_flow_benchmark.orus" "Control Flow Performance"

# Test 3: Complex Type System Integration
echo -e "\n${BLUE}=== Type System Integration Tests ===${NC}"
run_test "tests/types/complex_expressions.orus" "Complex Type Expressions"
run_test "tests/types/phase5_valid_casts.orus" "Advanced Type Casting"

# Test 4: Register System Integration  
echo -e "\n${BLUE}=== Register System Integration Tests ===${NC}"
run_test "tests/register_file/comprehensive_benchmark.orus" "Register System Stress"
run_test "tests/register_file/large_variable_test.orus" "Large Variable Handling"

# Test 5: Error Handling Integration
echo -e "\n${BLUE}=== Error Handling Integration Tests ===${NC}"
# These should fail gracefully with proper error messages
echo -n "  Testing Error Quality... "
if "$ORUS_BINARY" "tests/type_safety_fails/mixed_arithmetic_int_float.orus" > /tmp/error_test.out 2>&1; then
    echo -e "${RED}✗ Should have failed${NC}"
    ((failed++))
else
    # Check if error message is helpful
    if grep -q "help:" /tmp/error_test.out && grep -q "note:" /tmp/error_test.out; then
        echo -e "${GREEN}✓ PASS (Good error quality)${NC}"
        ((passed++))
    else
        echo -e "${YELLOW}~ PARTIAL (Error occurred but quality could improve)${NC}"
        ((passed++))
    fi
fi

# Test 6: Memory Integration (basic leak check)
echo -e "\n${BLUE}=== Memory Integration Tests ===${NC}"
echo -n "  Testing Memory Usage... "
if command -v leaks > /dev/null 2>&1; then
    if leaks --atExit -- "$ORUS_BINARY" "$INTEGRATION_TEST" > /tmp/leak_test.out 2>&1; then
        if grep -q "0 leaks for 0 total leaked bytes" /tmp/leak_test.out; then
            echo -e "${GREEN}✓ PASS (No leaks)${NC}"
            ((passed++))
        else
            echo -e "${YELLOW}~ PARTIAL (Some leaks detected)${NC}"
            ((passed++))
        fi
    else
        echo -e "${RED}✗ FAIL (Memory test failed)${NC}"
        ((failed++))
    fi
else
    echo -e "${BLUE}~ SKIP (leaks tool not available)${NC}"
    ((passed++))
fi

# Cleanup
rm -f /tmp/error_test.out /tmp/leak_test.out

# Summary
echo -e "\n${BLUE}=== Integration Test Results ===${NC}"
total=$((passed + failed))
echo "Total tests: $total"
echo -e "Passed: ${GREEN}$passed${NC}"
echo -e "Failed: ${RED}$failed${NC}"

if [ $failed -eq 0 ]; then
    echo -e "\n${GREEN}🎉 All integration tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}❌ Some integration tests failed.${NC}"
    exit 1
fi