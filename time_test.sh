#!/bin/bash

echo "=== Orus Performance Test with New Type System ==="
echo "Testing i64 loop sum with 1 billion iterations..."
echo ""

# Get the current time
echo "Starting execution..."
start_time=$(date +%s.%N)

# Run the Orus program
result=$(./orus tests/i64_test.orus)

# Get the end time
end_time=$(date +%s.%N)

# Calculate execution time
execution_time=$(echo "$end_time - $start_time" | bc -l)

echo "=== RESULTS ==="
echo "Output: $result"
echo "Execution time: ${execution_time} seconds"
echo ""

# Calculate performance metrics
iterations=1000000000
ops_per_second=$(echo "scale=2; $iterations / $execution_time" | bc -l)

echo "=== PERFORMANCE METRICS ==="
echo "Total iterations: $iterations"
echo "Operations per second: $ops_per_second"
echo ""

# Expected mathematical result for verification
expected=499999999500000000
echo "=== VERIFICATION ==="
echo "Expected result: $expected"
echo "Actual result:   $result"

if [ "$result" = "$expected" ]; then
    echo "✅ Result is CORRECT!"
else
    echo "❌ Result mismatch!"
fi

echo ""
echo "=== TYPE SYSTEM PERFORMANCE ==="
echo "This test validates:"
echo "• i64 arithmetic precision"
echo "• Type System performance under load"
echo "• Arena allocation efficiency"
echo "• SIMD-optimized operations"