#!/bin/bash

# Cast Test Suite Runner
echo "========================================"
echo "           ORUS CAST TEST SUITE         "
echo "========================================"

ORUS_DEBUG="./orus_debug"
TESTS_DIR="tests/types/casts"
PASSED=0
FAILED=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to run a single test
run_test() {
    local test_file=$1
    local test_name=$(basename "$test_file" .orus)
    
    echo -e "${YELLOW}Running test: $test_name${NC}"
    
    if $ORUS_DEBUG "$test_file" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ PASSED${NC}: $test_name"
        ((PASSED++))
    else
        echo -e "${RED}✗ FAILED${NC}: $test_name"
        echo "  Error output:"
        $ORUS_DEBUG "$test_file" 2>&1 | head -10 | sed 's/^/    /'
        ((FAILED++))
    fi
    echo
}

# Check if debug binary exists
if [[ ! -f "$ORUS_DEBUG" ]]; then
    echo -e "${RED}Error: $ORUS_DEBUG not found. Please build first with 'make debug'${NC}"
    exit 1
fi

# Run all cast tests
echo "Running cast tests..."
echo

for test_file in $TESTS_DIR/*.orus; do
    if [[ -f "$test_file" ]]; then
        run_test "$test_file"
    fi
done

# Summary
echo "========================================"
echo "              TEST SUMMARY              "
echo "========================================"
echo -e "Total tests run: $((PASSED + FAILED))"
echo -e "${GREEN}Passed: $PASSED${NC}"
echo -e "${RED}Failed: $FAILED${NC}"

if [[ $FAILED -eq 0 ]]; then
    echo -e "${GREEN}All tests passed! 🎉${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed. 😞${NC}"
    exit 1
fi