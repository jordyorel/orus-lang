#!/bin/bash
# Run macOS 'leaks' tool on all Orus test files in all test directories

set -e

ORUS_BIN="./orus"

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

for DIR in "${TEST_DIRS[@]}"; do
    if [ -d "$DIR" ]; then
        for f in "$DIR"/*.orus; do
            if [ -f "$f" ]; then
                echo "Running leaks on $f..."
                leaks -atExit -- "$ORUS_BIN" "$f"
            fi
        done
    fi

done
