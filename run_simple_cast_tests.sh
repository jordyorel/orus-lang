#!/bin/bash

echo "========================================"
echo "        SIMPLE CAST TEST SUITE         "
echo "========================================"

ORUS_DEBUG="./orus_debug"
TESTS_DIR="tests/types/casts"

# Check if debug binary exists
if [[ ! -f "$ORUS_DEBUG" ]]; then
    echo "Error: $ORUS_DEBUG not found. Please build first with 'make debug'"
    exit 1
fi

# Run each test individually with full output
for test_file in $TESTS_DIR/*.orus; do
    if [[ -f "$test_file" ]]; then
        test_name=$(basename "$test_file" .orus)
        echo "----------------------------------------"
        echo "Running: $test_name"
        echo "----------------------------------------"
        $ORUS_DEBUG "$test_file"
        echo
    fi
done

echo "========================================"
echo "             TESTS COMPLETE             "
echo "========================================"