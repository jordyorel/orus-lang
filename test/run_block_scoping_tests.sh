#!/bin/zsh

# Expected error lines from the test file
EXPECTED_ERRORS=(
    "print(inner)"         # line 10
    "print(loop_var)"      # line 19
    "mut x = 30"           # line 25
    "print(c)"             # line 36
    "print(b)"             # line 38
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
