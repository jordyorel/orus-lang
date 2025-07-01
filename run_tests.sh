#!/bin/bash

# Orus Test Runner Script
# Usage: ./run_tests.sh [category] [subcategory]
# Examples:
#   ./run_tests.sh                    # Run all tests
#   ./run_tests.sh types              # Run all type tests
#   ./run_tests.sh types i32          # Run only i32 tests
#   ./run_tests.sh conditionals basic # Run only basic conditional tests

set -e

ORUS_BIN="./orus"
TEST_DIR="tests"

# Colors for output
RED='\033[31m'
GREEN='\033[32m'
BLUE='\033[36m'
YELLOW='\033[33m'
NC='\033[0m' # No Color

if [ ! -f "$ORUS_BIN" ]; then
    echo -e "${RED}Error: orus binary not found. Run 'make' first.${NC}"
    exit 1
fi

run_tests() {
    local test_pattern="$1"
    local description="$2"
    
    echo -e "${BLUE}=== $description ===${NC}"
    
    local passed=0
    local failed=0
    local test_files
    
    if [ -z "$test_pattern" ]; then
        test_files=$(find "$TEST_DIR" -name "*.orus" | sort)
    else
        test_files=$(find "$TEST_DIR" -path "*$test_pattern*" -name "*.orus" | sort)
    fi
    
    if [ -z "$test_files" ]; then
        echo -e "${YELLOW}No tests found for pattern: $test_pattern${NC}"
        return
    fi
    
    for test_file in $test_files; do
        printf "Testing: $test_file ... "
        if $ORUS_BIN "$test_file" >/dev/null 2>&1; then
            printf "${GREEN}PASS${NC}\n"
            passed=$((passed + 1))
        else
            printf "${RED}FAIL${NC}\n"
            failed=$((failed + 1))
        fi
    done
    
    echo ""
    if [ $failed -eq 0 ]; then
        echo -e "${GREEN}✓ All $passed tests passed!${NC}"
    else
        echo -e "${RED}✗ $failed test(s) failed, $passed test(s) passed.${NC}"
    fi
    echo ""
    
    return $failed
}

# Main logic
total_failed=0

if [ $# -eq 0 ]; then
    # Run all tests by category
    categories=("types" "conditionals" "control_flow" "expressions" "formatting" "literals" "variables")
    
    echo -e "${BLUE}Running Orus Comprehensive Test Suite${NC}"
    echo "======================================"
    
    for category in "${categories[@]}"; do
        run_tests "$category" "$(echo ${category^} | sed 's/_/ /g') Tests"
        total_failed=$((total_failed + $?))
    done
    
elif [ $# -eq 1 ]; then
    # Run tests for specific category
    category="$1"
    run_tests "$category" "$(echo ${category^} | sed 's/_/ /g') Tests"
    total_failed=$?
    
elif [ $# -eq 2 ]; then
    # Run tests for specific subcategory
    category="$1"
    subcategory="$2"
    pattern="$category/$subcategory"
    run_tests "$pattern" "$category/$subcategory Tests"
    total_failed=$?
    
else
    echo "Usage: $0 [category] [subcategory]"
    echo ""
    echo "Examples:"
    echo "  $0                    # Run all tests"
    echo "  $0 types              # Run all type tests"
    echo "  $0 types i32          # Run only i32 tests"
    echo "  $0 conditionals basic # Run only basic conditional tests"
    exit 1
fi

# Final result
echo -e "${BLUE}=== Test Summary ===${NC}"
if [ $total_failed -eq 0 ]; then
    echo -e "${GREEN}✓ All test categories passed!${NC}"
    exit 0
else
    echo -e "${RED}✗ Some tests failed. Check output above for details.${NC}"
    exit 1
fi
