#!/bin/bash

# Comprehensive While Loop Test Runner
# This script runs all while loop tests and reports results

echo "🚀 Starting Comprehensive While Loop Test Suite"
echo "================================================"

# Test directory
TEST_DIR="tests/control_flow"
ORUS_BINARY="./orus_debug"

# Test files in order of complexity
TESTS=(
    "while_basic.orus"
    "while_break_continue.orus" 
    "while_nested.orus"
    "while_edge_cases.orus"
    "while_complex_expressions.orus"
    "while_error_conditions.orus"
)

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Function to run a single test
run_test() {
    local test_file="$1"
    local test_path="$TEST_DIR/$test_file"
    
    echo -e "\n${BLUE}Running:${NC} $test_file"
    echo "----------------------------------------"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    # Check if test file exists
    if [ ! -f "$test_path" ]; then
        echo -e "${RED}❌ FAIL:${NC} Test file not found: $test_path"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
    
    # Run the test and capture output
    if timeout 30s "$ORUS_BINARY" "$test_path" > "test_output_$(basename $test_file .orus).log" 2>&1; then
        echo -e "${GREEN}✅ PASS:${NC} $test_file completed successfully"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        
        # Show first few lines of output
        echo -e "${YELLOW}Sample output:${NC}"
        head -n 5 "test_output_$(basename $test_file .orus).log" | sed 's/^/  /'
        if [ $(wc -l < "test_output_$(basename $test_file .orus).log") -gt 5 ]; then
            echo "  ... (output truncated, see log file for full output)"
        fi
        
        return 0
    else
        echo -e "${RED}❌ FAIL:${NC} $test_file failed or timed out"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        
        # Show error output
        echo -e "${RED}Error output:${NC}"
        tail -n 10 "test_output_$(basename $test_file .orus).log" | sed 's/^/  /'
        
        return 1
    fi
}

# Function to check prerequisites
check_prerequisites() {
    echo "🔍 Checking prerequisites..."
    
    # Check if orus_debug binary exists
    if [ ! -f "$ORUS_BINARY" ]; then
        echo -e "${RED}❌ ERROR:${NC} Orus binary not found: $ORUS_BINARY"
        echo "Please run 'make debug' to build the binary first."
        exit 1
    fi
    
    # Check if test directory exists
    if [ ! -d "$TEST_DIR" ]; then
        echo -e "${RED}❌ ERROR:${NC} Test directory not found: $TEST_DIR"
        exit 1
    fi
    
    echo -e "${GREEN}✅${NC} Prerequisites satisfied"
}

# Function to display summary
show_summary() {
    echo ""
    echo "================================================"
    echo "🏁 Test Suite Summary"
    echo "================================================"
    echo -e "Total tests:  ${BLUE}$TOTAL_TESTS${NC}"
    echo -e "Passed:       ${GREEN}$PASSED_TESTS${NC}"
    echo -e "Failed:       ${RED}$FAILED_TESTS${NC}"
    
    if [ $FAILED_TESTS -eq 0 ]; then
        echo -e "\n${GREEN}🎉 All tests passed! While loop implementation is ready for production.${NC}"
        exit 0
    else
        echo -e "\n${RED}⚠️  Some tests failed. Review the logs and fix issues before production.${NC}"
        exit 1
    fi
}

# Function to clean up logs
cleanup_logs() {
    echo "🧹 Cleaning up old test logs..."
    rm -f test_output_*.log
}

# Main execution
main() {
    # Cleanup old logs
    cleanup_logs
    
    # Check prerequisites
    check_prerequisites
    
    # Run each test
    echo -e "\n🧪 Running while loop tests..."
    for test in "${TESTS[@]}"; do
        run_test "$test"
    done
    
    # Show summary
    show_summary
}

# Handle script arguments
case "${1:-}" in
    "clean")
        cleanup_logs
        echo "Test logs cleaned."
        ;;
    "list")
        echo "Available while loop tests:"
        printf '%s\n' "${TESTS[@]}"
        ;;
    "")
        main
        ;;
    *)
        if [[ "$1" == *.orus ]]; then
            # Run specific test
            run_test "$1"
        else
            echo "Usage: $0 [clean|list|test_file.orus]"
            echo "  clean     - Remove test log files"
            echo "  list      - List available tests"
            echo "  test.orus - Run specific test file"
            echo "  (no args) - Run all tests"
        fi
        ;;
esac