#!/bin/bash

echo "========================================================"
echo "Universal Arithmetic Benchmark: Cross-Language Testing"
echo "========================================================"
echo "Tests: Addition loops, mixed arithmetic, floating point"
echo "Expected results should be similar across languages"
echo ""

# Function to run benchmark with timing
run_benchmark() {
    local lang=$1
    local cmd=$2
    local file=$3
    
    echo "=== $lang ==="
    echo "Running: $cmd $file"
    echo "Results:"
    
    # Run the benchmark and capture timing
    time_output=$(mktemp)
    { time $cmd $file; } 2> $time_output
    
    echo ""
    echo "Timing:"
    cat $time_output | grep -E "(real|user|sys|execution time)"
    rm $time_output
    echo "----------------------------------------"
    echo ""
}

# Check if files exist and run benchmarks
cd "$(dirname "$0")"

if [[ -f "arithmetic_benchmark.orus" && -x "../../orus" ]]; then
    run_benchmark "Orus" "../../orus" "arithmetic_benchmark.orus"
fi

if [[ -f "arithmetic_benchmark.py" && $(command -v python3) ]]; then
    run_benchmark "Python 3" "python3" "arithmetic_benchmark.py"
fi

if [[ -f "arithmetic_benchmark.js" && $(command -v node) ]]; then
    run_benchmark "Node.js" "node" "arithmetic_benchmark.js"
fi

if [[ -f "arithmetic_benchmark.lua" && $(command -v lua) ]]; then
    run_benchmark "Lua" "lua" "arithmetic_benchmark.lua"
fi

echo "========================================================"
echo "Benchmark completed!"
echo "Compare execution times to evaluate relative performance"
echo "========================================================"