#!/bin/bash

# Comprehensive Loop Safety Test Suite
# Tests the progressive loop safety system with all edge cases

set -e

echo "======================================"
echo "🔒 ORUS LOOP SAFETY TEST SUITE"
echo "======================================"
echo

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Function to run a test and check results
run_test() {
    local test_name="$1"
    local test_file="$2"
    local expected_behavior="$3"
    local env_vars="$4"
    
    echo -e "${BLUE}🧪 Running: $test_name${NC}"
    echo "   File: $test_file"
    echo "   Expected: $expected_behavior"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Run the test with environment variables if specified
    if [ -n "$env_vars" ]; then
        echo "   Environment: $env_vars"
        if eval "$env_vars ../../orus $test_file" > /tmp/test_output.txt 2>&1; then
            test_result="COMPLETED"
        else
            test_result="ERROR"
        fi
    else
        if ../../orus "$test_file" > /tmp/test_output.txt 2>&1; then
            test_result="COMPLETED"
        else
            test_result="ERROR"
        fi
    fi
    
    # Check if the result matches expectations
    case "$expected_behavior" in
        "SHOULD_COMPLETE")
            if [ "$test_result" = "COMPLETED" ]; then
                echo -e "   ${GREEN}✅ PASS${NC} - Test completed as expected"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                echo -e "   ${RED}❌ FAIL${NC} - Test should have completed but failed"
                echo "   Output:"
                cat /tmp/test_output.txt | head -5
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            ;;
        "SHOULD_ERROR")
            if [ "$test_result" = "ERROR" ]; then
                echo -e "   ${GREEN}✅ PASS${NC} - Test failed as expected (safety limit)"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                echo -e "   ${RED}❌ FAIL${NC} - Test should have hit safety limit"
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            ;;
        "SHOULD_WARN")
            if grep -q "Warning:" /tmp/test_output.txt && [ "$test_result" = "COMPLETED" ]; then
                echo -e "   ${GREEN}✅ PASS${NC} - Test warned and completed"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                echo -e "   ${RED}❌ FAIL${NC} - Test should have warned but completed"
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            ;;
        "NO_WARNING")
            if ! grep -q "Warning:" /tmp/test_output.txt && [ "$test_result" = "COMPLETED" ]; then
                echo -e "   ${GREEN}✅ PASS${NC} - Test completed without warnings"
                PASSED_TESTS=$((PASSED_TESTS + 1))
            else
                echo -e "   ${RED}❌ FAIL${NC} - Test should not have warned"
                FAILED_TESTS=$((FAILED_TESTS + 1))
            fi
            ;;
    esac
    echo
}

# Navigate to test directory
cd "$(dirname "$0")"

echo "📍 Working directory: $(pwd)"
echo "🏗️  Building Orus..."
cd ../..
make clean && make > /dev/null 2>&1
cd tests/loop_safety
echo

# Test Category 1: Threshold Edge Cases
echo -e "${YELLOW}=== THRESHOLD EDGE CASES ===${NC}"
run_test "Exact Thresholds" "test_edge_thresholds.orus" "SHOULD_WARN"
run_test "10M Boundary" "test_10m_boundary.orus" "SHOULD_WARN"
run_test "10M + 1" "test_10m_plus_one.orus" "SHOULD_ERROR"

# Test Category 2: Nested Loops
echo -e "${YELLOW}=== NESTED LOOP TESTS ===${NC}"
run_test "Basic Nested Loops" "test_nested_loops.orus" "NO_WARNING"
run_test "Nested with Guards" "test_nested_guards.orus" "SHOULD_WARN"

# Test Category 3: Loop Type Consistency
echo -e "${YELLOW}=== LOOP TYPE CONSISTENCY ===${NC}"
run_test "For vs While Consistency" "test_loop_consistency.orus" "SHOULD_ERROR"
run_test "Static vs Dynamic Analysis" "test_static_dynamic.orus" "NO_WARNING"
run_test "Static 1.5M Loop" "test_static_1_5m.orus" "SHOULD_WARN"

# Test Category 4: Environment Variables
echo -e "${YELLOW}=== ENVIRONMENT VARIABLE TESTS ===${NC}"
run_test "Default Behavior" "test_env_vars.orus" "SHOULD_WARN"
run_test "Unlimited Mode" "test_env_vars.orus" "SHOULD_WARN" "ORUS_MAX_LOOP_ITERATIONS=0"
run_test "Custom 2M Limit" "test_custom_limit.orus" "SHOULD_ERROR" "ORUS_MAX_LOOP_ITERATIONS=2000000"
run_test "Custom Guard Threshold" "test_env_vars.orus" "SHOULD_WARN" "ORUS_LOOP_GUARD_THRESHOLD=50000"

# Test Category 5: Very Large Counts
echo -e "${YELLOW}=== VERY LARGE ITERATION TESTS ===${NC}"
run_test "50M Iterations (default)" "test_very_large.orus" "SHOULD_ERROR"
run_test "25M with 50M Limit" "test_custom_very_large.orus" "SHOULD_WARN" "ORUS_MAX_LOOP_ITERATIONS=50000000"

# Test Category 6: Stress Tests
echo -e "${YELLOW}=== STRESS TESTS ===${NC}"
run_test "Register Allocation Stress" "test_register_stress.orus" "NO_WARNING"
run_test "Comprehensive Test" "test_comprehensive.orus" "SHOULD_ERROR"

# Summary
echo "======================================"
echo -e "${BLUE}📊 TEST RESULTS SUMMARY${NC}"
echo "======================================"
echo "Total Tests:  $TOTAL_TESTS"
echo -e "Passed Tests: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed Tests: ${RED}$FAILED_TESTS${NC}"

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "\n${GREEN}🎉 ALL LOOP SAFETY TESTS PASSED!${NC}"
    echo "The progressive loop safety system is working perfectly."
    exit 0
else
    echo -e "\n${RED}❌ SOME TESTS FAILED${NC}"
    echo "Please review the failed tests above."
    exit 1
fi