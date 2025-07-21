#!/bin/zsh

# Expected error messages (must match exactly)
EXPECTED_ERRORS=(
    "Variable 'inner' is not in scope" 
    "Variable 'loop_var' is not in scope"
    "Cannot redeclare 'x' in same scope"
    "Variable 'c' is not in scope"
    "Variable 'b' is not in scope"
)

# Run compiler and capture errors
ERRORS=$(./orus test/block_scoping.orus 2>&1)

# Check each expected error
for pattern in "${EXPECTED_ERRORS[@]}"; do
    if ! echo "$ERRORS" | grep -q "$pattern"; then
        echo "❌ Test failed: Expected error containing '$pattern'"
        exit 1
    fi
done

# Check for unexpected errors
NUM_ERRORS=$(echo "$ERRORS" | wc -l)
if [ "$NUM_ERRORS" -ne "${#EXPECTED_ERRORS[@]}" ]; then
    echo "❌ Test failed: Expected ${#EXPECTED_ERRORS[@]} errors but got $NUM_ERRORS"
    echo "All errors:"
    echo "$ERRORS"
    exit 1
fi

echo "✅ All block scoping tests passed"
exit 0
